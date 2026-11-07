// Control-flow guard checks (division, indexing, parameters, loops, overflow).
#include "FprimeChecks.h"

#include "clang/Analysis/Analyses/Dominators.h"
#include "clang/Analysis/CFG.h"

#include "clang/AST/ParentMapContext.h"
#include "clang/AST/RecursiveASTVisitor.h"

using namespace clang::ast_matchers;

namespace clang {
namespace tidy {
namespace fprime {

namespace {

bool constNonZeroInit(const ValueDecl *V, ASTContext &Ctx) {
  const auto *VD = dyn_cast<VarDecl>(V);
  if (!VD || !VD->getType().isConstQualified() || !VD->getAnyInitializer())
    return false;
  Expr::EvalResult Result;
  if (!VD->getAnyInitializer()->EvaluateAsRValue(Result, Ctx))
    return false;
  if (Result.Val.isInt())
    return Result.Val.getInt() != 0;
  if (Result.Val.isFloat())
    return !Result.Val.getFloat().isZero();
  return false;
}

StringRef pathOf(const Stmt *S, const SourceManager &SM) { return SM.getFilename(SM.getExpansionLoc(S->getBeginLoc())); }

// Collects every expression of a kind within a statement.
template <typename NodeT> void collect(const Stmt *S, llvm::SmallVectorImpl<const NodeT *> &Out) {
  if (!S)
    return;
  if (const auto *N = dyn_cast<NodeT>(S))
    Out.push_back(N);
  for (const Stmt *C : S->children())
    collect<NodeT>(C, Out);
}

} // namespace

// cpp/fprime/division-guard (integer) and cpp/fprime/float-division-guard
class DivisionGuardCheckBase : public ClangTidyCheck {
public:
  DivisionGuardCheckBase(StringRef Name, ClangTidyContext *Ctx, bool Floating) : ClangTidyCheck(Name, Ctx), Floating(Floating) {}
  void registerMatchers(MatchFinder *F) override {
    auto Ops = Floating ? hasAnyOperatorName("/", "/=") : hasAnyOperatorName("/", "%", "/=", "%=");
    F->addMatcher(binaryOperator(Ops, hasAncestor(functionDecl().bind("fn"))).bind("op"), this);
  }
  void check(const MatchFinder::MatchResult &R) override {
    const auto *Op = R.Nodes.getNodeAs<BinaryOperator>("op");
    const auto *Fn = R.Nodes.getNodeAs<FunctionDecl>("fn");
    if (Op->isInstantiationDependent())
      return; // judged in the instantiations
    const Expr *Divisor = Op->getRHS();
    bool IsFloat = Divisor->getType()->isFloatingType() || Op->getLHS()->getType()->isFloatingType();
    if (IsFloat != Floating)
      return;
    const ValueDecl *V = accessedDecl(Divisor->IgnoreParenCasts());
    if (!V || isConstantValue(Divisor, *R.Context) || constNonZeroInit(V, *R.Context))
      return;
    if (guardedBefore(V, Op, Fn, *R.Context, /*Strict=*/Floating))
      return;
    if (Floating)
      diag(Op->getOperatorLoc(),
           "The floating-point divisor %0 is not checked against zero (or a minimum magnitude) before this division.")
          << V->getName();
    else
      diag(Op->getOperatorLoc(), "The divisor %0 is not checked against zero before this %1 operation.")
          << V->getName() << Op->getOpcodeStr();
  }

private:
  bool Floating;
};
class DivisionGuardCheck : public DivisionGuardCheckBase {
public:
  DivisionGuardCheck(StringRef N, ClangTidyContext *C) : DivisionGuardCheckBase(N, C, false) {}
};
class FloatDivisionGuardCheck : public DivisionGuardCheckBase {
public:
  FloatDivisionGuardCheck(StringRef N, ClangTidyContext *C) : DivisionGuardCheckBase(N, C, true) {}
};

// cpp/fprime/unchecked-array-index
class UncheckedArrayIndexCheck : public ClangTidyCheck {
public:
  using ClangTidyCheck::ClangTidyCheck;
  void registerMatchers(MatchFinder *F) override {
    F->addMatcher(arraySubscriptExpr(hasAncestor(functionDecl().bind("fn"))).bind("ae"), this);
  }
  void check(const MatchFinder::MatchResult &R) override {
    const auto *AE = R.Nodes.getNodeAs<ArraySubscriptExpr>("ae");
    const auto *Fn = R.Nodes.getNodeAs<FunctionDecl>("fn");
    if (inTestPath(pathOf(AE, *R.SourceManager)))
      return;
    const Expr *Idx = AE->getIdx();
    if (Idx->isInstantiationDependent())
      return; // judged in the instantiations; CodeQL also reports on the uninstantiated template
    const ValueDecl *V = accessedDecl(Idx->IgnoreParenCasts());
    if (!V || isa<ParmVarDecl>(V) || isConstantValue(Idx, *R.Context))
      return;
    if (AE->getBase()->IgnoreParenImpCasts()->getType()->isPointerType() &&
        !AE->getBase()->IgnoreParenImpCasts()->getType()->isArrayType())
      return;
    if (guardedBefore(V, AE, Fn, *R.Context))
      return;
    diag(AE->getBeginLoc(), "Index %0 is not checked against the array bounds before this access.") << V->getName();
  }
};

// cpp/fprime/signed-overflow
class SignedOverflowCheck : public ClangTidyCheck {
public:
  using ClangTidyCheck::ClangTidyCheck;
  void registerMatchers(MatchFinder *F) override {
    F->addMatcher(binaryOperator(hasAnyOperatorName("+", "-", "*"), hasAncestor(functionDecl().bind("fn"))).bind("op"), this);
  }
  void check(const MatchFinder::MatchResult &R) override {
    const auto *Op = R.Nodes.getNodeAs<BinaryOperator>("op");
    const auto *Fn = R.Nodes.getNodeAs<FunctionDecl>("fn");
    if (Op->isInstantiationDependent() || inTestPath(pathOf(Op, *R.SourceManager)))
      return;
    QualType T = fullyConvertedType(Op, *R.Context);
    if (!T->isSignedIntegerType() || T->isEnumeralType())
      return;
    const Expr *L = Op->getLHS()->IgnoreParenImpCasts();
    const Expr *Rr = Op->getRHS()->IgnoreParenImpCasts();
    if (isa<IntegerLiteral>(L) || isa<IntegerLiteral>(Rr) || isa<CharacterLiteral>(L) || isa<CharacterLiteral>(Rr))
      return;
    // Range-analysis shortcut: the operation can only overflow when some operand is unbounded.
    uint64_t Width = R.Context->getTypeSize(Op->getType());
    bool LB = bounded(L, Width, Op->getOpcode() == BO_Mul, *R.Context, Fn);
    bool RB = bounded(Rr, Width, Op->getOpcode() == BO_Mul, *R.Context, Fn);
    if (LB && RB)
      return;
    if (operandGuarded(L, Op, Fn, *R.Context) || operandGuarded(Rr, Op, Fn, *R.Context))
      return;
    diag(Op->getOperatorLoc(), "Signed %0 %1 may overflow; its operands are not range-checked beforehand.")
        << T.getUnqualifiedType().getAsString() << Op->getOpcodeStr();
  }

private:
  // Mirrors what CodeQL's range analysis can bound: literals, enumerators and sizeof (but not
  // const variables), values promoted or cast from a narrower type, conditionals and arithmetic
  // of bounded operands.
  // CodeQL judges signedness on the fully converted expression (e.g. `i64 += u32 + i32`).
  static QualType fullyConvertedType(const Expr *E, ASTContext &Ctx) {
    QualType T = E->getType();
    const Stmt *Cur = E;
    while (true) {
      const auto &Parents = Ctx.getParents(*Cur);
      if (Parents.empty())
        break;
      const auto *P = Parents[0].get<Stmt>();
      if (!P || !(isa<ImplicitCastExpr>(P) || isa<ParenExpr>(P)))
        break;
      T = cast<Expr>(P)->getType();
      Cur = P;
    }
    return T;
  }
  // A local whose only definition is a constant initialiser has a known range in CodeQL's SSA.
  static bool constantLocal(const ValueDecl *V, const FunctionDecl *Fn, ASTContext &Ctx) {
    const auto *VD = dyn_cast_or_null<VarDecl>(V);
    if (!VD || !VD->isLocalVarDecl() || isa<ParmVarDecl>(VD) || !VD->getInit() || !isConstantValue(VD->getInit(), Ctx))
      return false;
    return !writtenAnywhere(Fn->getBody(), VD);
  }
  static bool writtenAnywhere(const Stmt *S, const ValueDecl *V) {
    if (!S)
      return false;
    if (const auto *B = dyn_cast<BinaryOperator>(S); B && B->isAssignmentOp() && accessedDecl(B->getLHS()->IgnoreParenImpCasts()) == V)
      return true;
    if (const auto *U = dyn_cast<UnaryOperator>(S); U && (U->isIncrementDecrementOp() || U->getOpcode() == UO_AddrOf) &&
                                                   accessedDecl(U->getSubExpr()->IgnoreParenImpCasts()) == V)
      return true;
    for (const Stmt *C : S->children())
      if (writtenAnywhere(C, V))
        return true;
    return false;
  }
  static bool bounded(const Expr *E, uint64_t Width, bool Mul, ASTContext &Ctx, const FunctionDecl *Fn) {
    E = E->IgnoreParenImpCasts();
    if (isa<IntegerLiteral>(E) || isa<CharacterLiteral>(E) || isa<CXXBoolLiteralExpr>(E) ||
        isa<UnaryExprOrTypeTraitExpr>(E))
      return true;
    if (const auto *DRE = dyn_cast<DeclRefExpr>(E))
      if (isa<EnumConstantDecl>(DRE->getDecl()) || constantLocal(DRE->getDecl(), Fn, Ctx))
        return true;
    if (const auto *C = dyn_cast<ConditionalOperator>(E))
      return bounded(C->getTrueExpr(), Width, Mul, Ctx, Fn) && bounded(C->getFalseExpr(), Width, Mul, Ctx, Fn);
    if (const auto *B = dyn_cast<BinaryOperator>(E))
      if (B->getOpcode() == BO_Add || B->getOpcode() == BO_Sub || B->getOpcode() == BO_Mul)
        return bounded(B->getLHS(), Width, Mul, Ctx, Fn) && bounded(B->getRHS(), Width, Mul, Ctx, Fn);
    if (Mul)
      return false; // CodeQL's range analysis does not bound products of variables
    if (const auto *Cast = dyn_cast<ExplicitCastExpr>(E))
      if (!Cast->getSubExpr()->getType()->isPointerType())
        return bounded(Cast->getSubExpr(), Width, Mul, Ctx, Fn) ||
               Ctx.getTypeSize(Cast->getSubExpr()->getType()) < Width;
    return E->getType()->isIntegerType() && Ctx.getTypeSize(E->getType()) < Width;
  }
  static bool operandGuarded(const Expr *Operand, const Stmt *Use, const FunctionDecl *Fn, ASTContext &Ctx) {
    const ValueDecl *V = accessedDecl(Operand);
    return V && guardedBefore(V, Use, Fn, Ctx);
  }
};

// cpp/fprime/checking-parameter-values
class CheckingParameterValuesCheck : public ClangTidyCheck {
public:
  using ClangTidyCheck::ClangTidyCheck;
  void registerMatchers(MatchFinder *F) override {
    F->addMatcher(functionDecl(isDefinition(), hasBody(stmt())).bind("fn"), this);
  }
  void check(const MatchFinder::MatchResult &R) override {
    const auto *Fn = R.Nodes.getNodeAs<FunctionDecl>("fn");
    if (!isa<CXXMethodDecl>(Fn) && (Fn->getStorageClass() == SC_Static || !Fn->isExternallyVisible()))
      return;
    for (const ParmVarDecl *P : Fn->parameters()) {
      bool Pointer = P->getType()->isPointerType();
      bool Integer = P->getType()->isIntegerType() && !P->getType()->isBooleanType() && !P->getType()->isEnumeralType();
      if (!Pointer && !Integer)
        continue;
      llvm::SmallVector<std::pair<const Expr *, const Stmt *>, 8> Uses;
      unsafeUses(Fn->getBody(), P, Pointer, Uses);
      for (auto [Access, Use] : Uses) {
        if (isCheckingAccess(P, Access, Fn, *R.Context))
          continue;
        if (guardedBefore(P, Use, Fn, *R.Context, /*Strict=*/true))
          continue;
        if (reassignedBefore(P, Use, Fn, *R.Context))
          continue;
        diag(Access->getBeginLoc(),
             "Parameter %0 is %1 without first being checked (e.g. with FW_ASSERT or a condition).")
            << P->getName() << (Pointer ? "dereferenced" : "used as an array index or divisor");
      }
    }
  }

private:
  static const Expr *accessOf(const Expr *E, const ParmVarDecl *P) {
    const Expr *Inner = E->IgnoreParenCasts();
    return accessedDecl(Inner) == P ? Inner : nullptr;
  }
  // An assignment or ++/-- of the parameter that dominates the use ends CodeQL's uncheckedFlow.
  static bool reassignedBefore(const ParmVarDecl *P, const Stmt *Use, const FunctionDecl *Fn, ASTContext &Ctx) {
    llvm::SmallVector<const Stmt *, 4> Defs;
    collectDefs(Fn->getBody(), P, Defs);
    return definedOnEveryPath(Defs, Use, Fn, Ctx);
  }
  static void collectDefs(const Stmt *S, const ParmVarDecl *P, llvm::SmallVectorImpl<const Stmt *> &Out) {
    if (!S || isa<LambdaExpr>(S))
      return;
    if (const auto *B = dyn_cast<BinaryOperator>(S); B && B->isAssignmentOp() && accessedDecl(B->getLHS()->IgnoreParenImpCasts()) == P)
      Out.push_back(B);
    if (const auto *U = dyn_cast<UnaryOperator>(S); U && U->isIncrementDecrementOp() && accessedDecl(U->getSubExpr()->IgnoreParenImpCasts()) == P)
      Out.push_back(U);
    for (const Stmt *C : S->children())
      collectDefs(C, P, Out);
  }
  static void unsafeUses(const Stmt *S, const ParmVarDecl *P, bool Pointer,
                         llvm::SmallVectorImpl<std::pair<const Expr *, const Stmt *>> &Out) {
    if (!S || isa<LambdaExpr>(S))
      return;
    if (Pointer) {
      if (const auto *U = dyn_cast<UnaryOperator>(S); U && U->getOpcode() == UO_Deref) {
        if (const Expr *A = accessOf(U->getSubExpr(), P))
          Out.push_back({A, U});
      } else if (const auto *AE = dyn_cast<ArraySubscriptExpr>(S)) {
        if (const Expr *A = accessOf(AE->getBase(), P))
          Out.push_back({A, AE});
      } else if (const auto *ME = dyn_cast<MemberExpr>(S); ME && ME->isArrow()) {
        if (const Expr *A = accessOf(ME->getBase(), P))
          Out.push_back({A, ME});
      } else if (const auto *B = dyn_cast<BinaryOperator>(S);
                 B && (B->getOpcode() == BO_Add || B->getOpcode() == BO_Sub) && B->getType()->isPointerType()) {
        if (const Expr *A = accessOf(B->getLHS(), P))
          Out.push_back({A, B});
        if (const Expr *A = accessOf(B->getRHS(), P))
          Out.push_back({A, B});
      }
    } else {
      if (const auto *AE = dyn_cast<ArraySubscriptExpr>(S)) {
        if (const Expr *A = accessOf(AE->getIdx(), P))
          Out.push_back({A, AE});
      } else if (const auto *B = dyn_cast<BinaryOperator>(S);
                 B && (B->getOpcode() == BO_Div || B->getOpcode() == BO_Rem)) {
        if (const Expr *A = accessOf(B->getRHS(), P))
          Out.push_back({A, B});
      }
    }
    for (const Stmt *C : S->children())
      unsafeUses(C, P, Pointer, Out);
  }
};

// cpp/fprime/checking-return-values
class CheckingReturnValuesCheck : public ClangTidyCheck {
public:
  using ClangTidyCheck::ClangTidyCheck;
  void registerMatchers(MatchFinder *F) override {
    F->addMatcher(callExpr(callee(functionDecl(unless(returns(voidType()))).bind("callee")),
                           hasAncestor(functionDecl().bind("fn")))
                      .bind("call"),
                  this);
  }
  void check(const MatchFinder::MatchResult &R) override {
    const auto *Call = R.Nodes.getNodeAs<CallExpr>("call");
    const auto *Callee = R.Nodes.getNodeAs<FunctionDecl>("callee");
    const auto *Fn = R.Nodes.getNodeAs<FunctionDecl>("fn");
    if (Call->isInstantiationDependent() || Callee->getType()->isDependentType() || Callee->getReturnType()->isVoidType())
      return;
    if (whitelisted(Callee))
      return;
    const Stmt *Parent = parentStmt(Call, *R.Context);
    std::string Name = Callee->getNameAsString();
    if (inVoidContext(Call, Parent, *R.Context)) {
      diag(Call->getBeginLoc(), "The return value of non-void function %0 is not checked.") << Name;
      return;
    }
    const VarDecl *Stored = storedInto(Call, Parent);
    if (Stored && !isa<ParmVarDecl>(Stored) && !Stored->getType()->isReferenceType() && !Stored->isConstexpr() &&
        !usedBeforeRedefinition(Stored, Parent, Fn, *R.Context))
      diag(Call->getBeginLoc(), "%0's return value is stored but not checked.") << Name;
  }

private:
  static bool whitelisted(const FunctionDecl *F) {
    if (F->getDeclName().isIdentifier())
      return F->getName().startswith("__builtin_");
    if (F->isOverloadedOperator()) {
      switch (F->getOverloadedOperator()) {
      case OO_Equal: case OO_PlusEqual: case OO_MinusEqual: case OO_StarEqual: case OO_SlashEqual:
      case OO_PercentEqual: case OO_AmpEqual: case OO_PipeEqual: case OO_CaretEqual: case OO_LessLessEqual:
      case OO_GreaterGreaterEqual: case OO_PlusPlus: case OO_MinusMinus:
        return true;
      default:
        return false;
      }
    }
    return false;
  }
  static const Stmt *parentStmt(const Stmt *S, ASTContext &Ctx) {
    const Stmt *Cur = S;
    while (true) {
      const auto &Parents = Ctx.getParents(*Cur);
      if (Parents.empty())
        return nullptr;
      const Stmt *P = Parents[0].get<Stmt>();
      if (!P)
        return nullptr;
      if (isa<ExprWithCleanups>(P) || isa<ConstantExpr>(P) || isa<MaterializeTemporaryExpr>(P) ||
          isa<CXXBindTemporaryExpr>(P)) {
        Cur = P;
        continue;
      }
      return P;
    }
  }
  static bool inVoidContext(const CallExpr *Call, const Stmt *Parent, ASTContext &Ctx) {
    if (!Parent)
      return false;
    if (isa<CompoundStmt>(Parent) || isa<CaseStmt>(Parent) || isa<DefaultStmt>(Parent) || isa<LabelStmt>(Parent))
      return true;
    if (const auto *For = dyn_cast<ForStmt>(Parent))
      return For->getInit() == Call || For->getInc() == Call || containsDirect(For->getInit(), Call) ||
             containsDirect(For->getInc(), Call);
    if (const auto *If = dyn_cast<IfStmt>(Parent))
      return If->getThen() == Call || If->getElse() == Call;
    if (const auto *W = dyn_cast<WhileStmt>(Parent))
      return W->getBody() == Call;
    if (const auto *D = dyn_cast<DoStmt>(Parent))
      return D->getBody() == Call;
    if (const auto *B = dyn_cast<BinaryOperator>(Parent))
      return B->getOpcode() == BO_Comma && B->getLHS() == Call;
    return false;
  }
  static bool containsDirect(const Stmt *S, const Stmt *Target) {
    if (!S)
      return false;
    if (const auto *E = dyn_cast<Expr>(S))
      return E->IgnoreParenImpCasts() == Target;
    return false;
  }
  static const VarDecl *storedInto(const CallExpr *Call, const Stmt *Parent) {
    if (!Parent)
      return nullptr;
    if (const auto *B = dyn_cast<BinaryOperator>(Parent); B && B->getOpcode() == BO_Assign) {
      if (B->getRHS()->IgnoreParenImpCasts() == Call)
        if (const auto *DRE = dyn_cast<DeclRefExpr>(B->getLHS()->IgnoreParenImpCasts()))
          if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl()); VD && VD->isLocalVarDeclOrParm())
            return VD;
      return nullptr;
    }
    if (const auto *DS = dyn_cast<DeclStmt>(Parent))
      for (const Decl *D : DS->decls())
        if (const auto *VD = dyn_cast<VarDecl>(D); VD && VD->getInit() && VD->getInit()->IgnoreParenImpCasts() == Call &&
                                                   VD->isLocalVarDecl())
          return VD;
    // Initialisers are wrapped in implicit casts: look through them.
    return nullptr;
  }
  static bool usedBeforeRedefinition(const VarDecl *V, const Stmt *Def, const FunctionDecl *Fn, ASTContext &Ctx) {
    CFG::BuildOptions BuildOpts;
    BuildOpts.setAllAlwaysAdd();
    std::unique_ptr<CFG> G = CFG::buildCFG(Fn, Fn->getBody(), &Ctx, BuildOpts);
    if (!G)
      return true;
    unsigned DefIndex = 0;
    const CFGBlock *DefBlock = blockOfOrParent(*G, Def, Ctx, &DefIndex);
    if (!DefBlock)
      return true;
    std::set<const CFGBlock *> Visited;
    llvm::SmallVector<std::pair<const CFGBlock *, unsigned>, 16> Work;
    Work.push_back({DefBlock, DefIndex + 1});
    while (!Work.empty()) {
      auto [B, From] = Work.pop_back_val();
      bool Redefined = false;
      for (unsigned I = From; I < B->size() && !Redefined; ++I) {
        auto CS = (*B)[I].getAs<CFGStmt>();
        if (!CS)
          continue;
        const auto *DRE = dyn_cast<DeclRefExpr>(CS->getStmt());
        if (!DRE || DRE->getDecl() != V)
          continue;
        const auto &Parents = Ctx.getParents(*DRE);
        const auto *PB = Parents.empty() ? nullptr : Parents[0].get<BinaryOperator>();
        if (PB && PB->getOpcode() == BO_Assign && PB->getLHS() == DRE)
          Redefined = true; // overwritten without being read
        else
          return true;
      }
      if (Redefined)
        continue;
      for (const CFGBlock *Succ : B->succs())
        if (Succ && Visited.insert(Succ).second)
          Work.push_back({Succ, 0});
    }
    return false;
  }
  static bool readLater(const VarDecl *V, const Stmt *Def, const Stmt *Body, const SourceManager &SM) {
    llvm::SmallVector<const DeclRefExpr *, 8> Refs;
    collect<DeclRefExpr>(Body, Refs);
    SourceLocation DefLoc = SM.getExpansionLoc(Def->getBeginLoc());
    for (const DeclRefExpr *Ref : Refs)
      if (Ref->getDecl() == V && SM.isBeforeInTranslationUnit(DefLoc, SM.getExpansionLoc(Ref->getBeginLoc())))
        return true;
    return false;
  }
};

// cpp/fprime/loop-bounds
class LoopBoundsCheck : public ClangTidyCheck {
public:
  using ClangTidyCheck::ClangTidyCheck;
  void registerMatchers(MatchFinder *F) override {
    F->addMatcher(stmt(anyOf(forStmt(), whileStmt(), doStmt())).bind("loop"), this);
  }
  void check(const MatchFinder::MatchResult &R) override {
    const auto *Loop = R.Nodes.getNodeAs<Stmt>("loop");
    const Expr *Cond = nullptr;
    const Stmt *Body = nullptr;
    const Expr *Inc = nullptr;
    if (const auto *F = dyn_cast<ForStmt>(Loop)) {
      Cond = F->getCond();
      Body = F->getBody();
      Inc = F->getInc();
    } else if (const auto *W = dyn_cast<WhileStmt>(Loop)) {
      Cond = W->getCond();
      Body = W->getBody();
    } else if (const auto *D = dyn_cast<DoStmt>(Loop)) {
      Cond = D->getCond();
      Body = D->getBody();
    }
    if (markedNonTerminating(Loop, *R.SourceManager, R.Context->getLangOpts()))
      return;
    if (Cond && (Cond->isInstantiationDependent() || isConstantValue(Cond, *R.Context)))
      return;
    llvm::SmallVector<const BinaryOperator *, 4> Rels;
    collect<BinaryOperator>(Cond, Rels);
    bool AnyBound = false;
    llvm::SmallVector<std::pair<const ValueDecl *, bool>, 4> Counters; // (var, upper?)
    for (const BinaryOperator *Rel : Rels) {
      if (!Rel->isRelationalOp())
        continue;
      bool LessOnLeft = Rel->getOpcode() == BO_LT || Rel->getOpcode() == BO_LE;
      const Expr *Lesser = (LessOnLeft ? Rel->getLHS() : Rel->getRHS())->IgnoreParenImpCasts();
      const Expr *Greater = (LessOnLeft ? Rel->getRHS() : Rel->getLHS())->IgnoreParenImpCasts();
      if (const ValueDecl *V = accessedDecl(Lesser); V && validBound(Greater, Loop, *R.Context)) {
        AnyBound = true;
        Counters.push_back({V, true});
      }
      if (const ValueDecl *V = accessedDecl(Greater); V && validBound(Lesser, Loop, *R.Context)) {
        AnyBound = true;
        Counters.push_back({V, false});
      }
    }
    if (!AnyBound) {
      diag(Loop->getBeginLoc(), "This loop does not have a fixed bound.");
      return;
    }
    bool Safe = false;
    llvm::SmallVector<std::pair<const ValueDecl *, bool>, 4> Unsafe;
    for (auto [V, Upper] : Counters) {
      if (alwaysStepped(V, Upper, Body, Inc, *R.Context))
        Safe = true;
      else
        Unsafe.push_back({V, Upper});
    }
    if (Safe)
      return;
    for (auto [V, Upper] : Unsafe)
      diag(Loop->getBeginLoc(), "The loop counter %0 is not always %1 in the loop body.")
          << V->getName() << (Upper ? "incremented" : "decremented");
  }

private:
  static bool markedNonTerminating(const Stmt *Loop, const SourceManager &SM, const LangOptions &LO) {
    SourceLocation Begin = SM.getExpansionLoc(Loop->getBeginLoc());
    FileID FID = SM.getFileID(Begin);
    unsigned Line = SM.getSpellingLineNumber(Begin);
    if (Line < 2)
      return false;
    SourceLocation From = SM.translateLineCol(FID, Line > 3 ? Line - 3 : 1, 1);
    if (From.isInvalid())
      return false;
    StringRef Text = Lexer::getSourceText(CharSourceRange::getCharRange(From, Begin), SM, LO);
    // CodeQL's getCommentedElement(): the comment must be the last thing before the loop.
    size_t Pos = Text.rfind("@non-terminating@");
    if (Pos == StringRef::npos)
      return false;
    StringRef Tail = Text.substr(Pos);
    size_t End = Tail.find("*/");
    if (End != StringRef::npos)
      Tail = Tail.substr(End + 2);
    else
      Tail = Tail.substr(Tail.find('\n') == StringRef::npos ? Tail.size() : Tail.find('\n'));
    return Tail.trim().empty();
  }
  // Constant, const variable, or variable not written inside the loop.
  static bool validBound(const Expr *E, const Stmt *Loop, ASTContext &Ctx) {
    if (isa<CharacterLiteral>(E))
      return false;
    if (isConstantValue(E, Ctx))
      return true;
    const ValueDecl *V = accessedDecl(E);
    if (!V)
      return false;
    if (V->getType().isConstQualified())
      return true;
    return !writtenIn(Loop, V);
  }
  static bool writtenIn(const Stmt *S, const ValueDecl *V) {
    if (!S)
      return false;
    if (const auto *B = dyn_cast<BinaryOperator>(S); B && B->isAssignmentOp() && accessedDecl(B->getLHS()->IgnoreParenImpCasts()) == V)
      return true;
    if (const auto *U = dyn_cast<UnaryOperator>(S); U && U->isIncrementDecrementOp() && accessedDecl(U->getSubExpr()->IgnoreParenImpCasts()) == V)
      return true;
    for (const Stmt *C : S->children())
      if (writtenIn(C, V))
        return true;
    return false;
  }
  static bool isStep(const Stmt *S, const ValueDecl *V, bool Upper, ASTContext &Ctx) {
    if (!S)
      return false;
    if (const auto *U = dyn_cast<UnaryOperator>(S))
      return (Upper ? U->isIncrementOp() : U->isDecrementOp()) && accessedDecl(U->getSubExpr()->IgnoreParenImpCasts()) == V;
    if (const auto *B = dyn_cast<BinaryOperator>(S)) {
      if (accessedDecl(B->getLHS()->IgnoreParenImpCasts()) != V)
        return false;
      Expr::EvalResult Res;
      if (B->getOpcode() == (Upper ? BO_AddAssign : BO_SubAssign))
        return B->getRHS()->EvaluateAsInt(Res, Ctx) && Res.Val.getInt() > 0;
      if (B->getOpcode() == BO_Assign) {
        const auto *Inner = dyn_cast<BinaryOperator>(B->getRHS()->IgnoreParenImpCasts());
        if (!Inner || Inner->getOpcode() != (Upper ? BO_Add : BO_Sub))
          return false;
        const Expr *L = Inner->getLHS()->IgnoreParenImpCasts(), *Rr = Inner->getRHS()->IgnoreParenImpCasts();
        if (Upper)
          return (accessedDecl(L) == V && Rr->EvaluateAsInt(Res, Ctx) && Res.Val.getInt() > 0) ||
                 (accessedDecl(Rr) == V && L->EvaluateAsInt(Res, Ctx) && Res.Val.getInt() > 0);
        return accessedDecl(L) == V && Rr->EvaluateAsInt(Res, Ctx) && Res.Val.getInt() > 0;
      }
    }
    return false;
  }
  static bool hasContinue(const Stmt *S) {
    if (!S)
      return false;
    if (isa<ContinueStmt>(S))
      return true;
    if (isa<ForStmt>(S) || isa<WhileStmt>(S) || isa<DoStmt>(S) || isa<CXXForRangeStmt>(S))
      return false; // continue inside a nested loop targets that loop
    for (const Stmt *C : S->children())
      if (hasContinue(C))
        return true;
    return false;
  }
  // The counter is stepped on every path around the loop: in the for-increment, or by an
  // unconditional top-level body statement that no `continue` can skip.
  static bool alwaysStepped(const ValueDecl *V, bool Upper, const Stmt *Body, const Expr *Inc, ASTContext &Ctx) {
    if (Inc && (isStep(Inc->IgnoreParenImpCasts(), V, Upper, Ctx) || stepInCommaList(Inc, V, Upper, Ctx)))
      return true;
    llvm::SmallVector<const Stmt *, 16> Stmts;
    if (const auto *CS = dyn_cast_or_null<CompoundStmt>(Body))
      Stmts.append(CS->body_begin(), CS->body_end());
    else if (Body)
      Stmts.push_back(Body);
    for (size_t I = 0; I < Stmts.size(); ++I) {
      const Stmt *S = Stmts[I];
      if (const auto *E = dyn_cast<Expr>(S); E && containsStep(E, V, Upper, Ctx)) {
        for (size_t J = 0; J < I; ++J)
          if (hasContinue(Stmts[J]))
            return false;
        return true;
      }
    }
    return false;
  }
  static bool containsStep(const Stmt *S, const ValueDecl *V, bool Upper, ASTContext &Ctx) {
    if (!S || isa<LambdaExpr>(S))
      return false;
    if (isStep(S, V, Upper, Ctx))
      return true;
    for (const Stmt *C : S->children())
      if (containsStep(C, V, Upper, Ctx))
        return true;
    return false;
  }
  static bool stepInCommaList(const Expr *E, const ValueDecl *V, bool Upper, ASTContext &Ctx) {
    if (const auto *B = dyn_cast<BinaryOperator>(E->IgnoreParenImpCasts()); B && B->getOpcode() == BO_Comma)
      return stepInCommaList(B->getLHS(), V, Upper, Ctx) || stepInCommaList(B->getRHS(), V, Upper, Ctx);
    return isStep(E->IgnoreParenImpCasts(), V, Upper, Ctx);
  }
};

// cpp/fprime/unguarded-unsigned-subtraction
class UnguardedUnsignedSubtractionCheck : public ClangTidyCheck {
public:
  using ClangTidyCheck::ClangTidyCheck;
  void registerMatchers(MatchFinder *F) override {
    F->addMatcher(binaryOperator(hasOperatorName("-"), hasAncestor(functionDecl().bind("fn"))).bind("sub"), this);
  }
  void check(const MatchFinder::MatchResult &R) override {
    const auto *Sub = R.Nodes.getNodeAs<BinaryOperator>("sub");
    const auto *Fn = R.Nodes.getNodeAs<FunctionDecl>("fn");
    if (!Sub->getType()->isUnsignedIntegerType())
      return;
    Expr::EvalResult K;
    if (!Sub->getRHS()->EvaluateAsInt(K, *R.Context))
      return;
    llvm::SmallVector<const CallExpr *, 4> Calls;
    collect<CallExpr>(Sub->getLHS(), Calls);
    const CallExpr *Minuend = nullptr;
    for (const CallExpr *C : Calls)
      if (isSizeAccessor(C)) {
        Minuend = C;
        break;
      }
    if (!Minuend)
      return;
    double KVal = K.Val.getInt().getSExtValue();
    if (hasCheck(Minuend, KVal, Fn, *R.Context))
      return;
    diag(Sub->getBeginLoc(),
         "Unsigned subtraction from %0() with no check that it is at least the subtracted constant; this underflows "
         "instead of going negative when the size is too small.")
        << Minuend->getDirectCallee()->getName();
  }

private:
  static bool isSizeAccessor(const CallExpr *C) {
    const FunctionDecl *F = C->getDirectCallee();
    if (!F || !F->getDeclName().isIdentifier())
      return false;
    static const char *Names[] = {"getSize", "getBuffLength", "getDataSize", "getBuffCapacity",
                                  "getDeserializeSizeLeft", "getSerializeSizeLeft"};
    return llvm::is_contained(Names, F->getName());
  }
  static const Decl *receiver(const CallExpr *C) {
    const auto *MC = dyn_cast<CXXMemberCallExpr>(C);
    if (!MC)
      return nullptr;
    const Expr *Obj = MC->getImplicitObjectArgument()->IgnoreParenImpCasts();
    if (isa<CXXThisExpr>(Obj))
      return MC->getMethodDecl()->getParent();
    return accessedDecl(Obj);
  }
  static bool sameReceiver(const CallExpr *A, const CallExpr *B) {
    const Decl *RA = receiver(A), *RB = receiver(B);
    if (RA && RB)
      return RA == RB;
    return !RA && !RB;
  }
  static bool readsSameSize(const Expr *Read, const CallExpr *Minuend) {
    const CallExpr *Acc = dyn_cast<CallExpr>(Read);
    if (!Acc)
      if (const auto *DRE = dyn_cast<DeclRefExpr>(Read))
        if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl()); VD && VD->isLocalVarDecl() && VD->getInit())
          Acc = dyn_cast<CallExpr>(VD->getInit()->IgnoreParenImpCasts());
    if (!Acc || !isSizeAccessor(Acc))
      return false;
    return Acc->getDirectCallee()->getName() == Minuend->getDirectCallee()->getName() && sameReceiver(Acc, Minuend);
  }
  static bool hasCheck(const CallExpr *Minuend, double K, const FunctionDecl *Fn, ASTContext &Ctx) {
    llvm::SmallVector<const BinaryOperator *, 16> Ops;
    collect<BinaryOperator>(Fn->getBody(), Ops);
    for (const BinaryOperator *Rel : Ops) {
      if (!Rel->isRelationalOp())
        continue;
      for (int Side = 0; Side < 2; ++Side) {
        const Expr *SizeSide = Side ? Rel->getRHS() : Rel->getLHS();
        const Expr *Bound = Side ? Rel->getLHS() : Rel->getRHS();
        llvm::SmallVector<const Expr *, 8> Reads;
        collect<Expr>(SizeSide, Reads);
        bool ReadsSize = llvm::any_of(Reads, [&](const Expr *E) { return readsSameSize(E, Minuend); });
        if (!ReadsSize)
          continue;
        Expr::EvalResult BV;
        if (!Bound->EvaluateAsInt(BV, Ctx) || BV.Val.getInt().getSExtValue() >= K - 1.0)
          return true;
      }
    }
    return false;
  }
};

void registerGuardChecks(ClangTidyCheckFactories &F) {
  F.registerCheck<DivisionGuardCheck>("fprime-division-guard");
  F.registerCheck<FloatDivisionGuardCheck>("fprime-float-division-guard");
  F.registerCheck<UncheckedArrayIndexCheck>("fprime-unchecked-array-index");
  F.registerCheck<SignedOverflowCheck>("fprime-signed-overflow");
  F.registerCheck<CheckingParameterValuesCheck>("fprime-checking-parameter-values");
  F.registerCheck<CheckingReturnValuesCheck>("fprime-checking-return-values");
  F.registerCheck<LoopBoundsCheck>("fprime-loop-bounds");
  F.registerCheck<UnguardedUnsignedSubtractionCheck>("fprime-unguarded-unsigned-subtraction");
}

} // namespace fprime
} // namespace tidy
} // namespace clang
