// JPL coding-standard style checks: literals, statements per line, for headers,
// integer types, function pointers, preprocessor use, float narrowing, boolean side effects.
#include "FprimeChecks.h"

#include "clang/AST/ParentMapContext.h"
#include "clang/Lex/PPCallbacks.h"
#include "clang/Lex/Preprocessor.h"

#include <map>
#include <regex>

using namespace clang::ast_matchers;

namespace clang {
namespace tidy {
namespace fprime {

namespace {

template <typename NodeT> void collect(const Stmt *S, llvm::SmallVectorImpl<const NodeT *> &Out) {
  if (!S)
    return;
  if (const auto *N = dyn_cast<NodeT>(S))
    Out.push_back(N);
  for (const Stmt *C : S->children())
    collect<NodeT>(C, Out);
}

bool isHeaderPath(StringRef Path) {
  return Path.endswith(".h") || Path.endswith(".hpp") || Path.endswith(".hh") || Path.endswith(".hxx") || Path.endswith(".H") ||
         Path.endswith(".inl") || Path.endswith(".ipp");
}

} // namespace

// cpp/fprime/magic-numbers
class MagicNumbersCheck : public ClangTidyCheck {
public:
  using ClangTidyCheck::ClangTidyCheck;
  void registerMatchers(MatchFinder *F) override {
    F->addMatcher(integerLiteral().bind("lit"), this);
    F->addMatcher(floatLiteral().bind("lit"), this);
  }
  void check(const MatchFinder::MatchResult &R) override {
    const auto *Lit = R.Nodes.getNodeAs<Expr>("lit");
    const SourceManager &SM = *R.SourceManager;
    if (Lit->getBeginLoc().isMacroID())
      return;
    StringRef Path = SM.getFilename(Lit->getBeginLoc());
    if (inTestPath(Path) || Path.contains("/Crc32/"))
      return;
    if (trivial(Lit, *R.Context) || inConstantDefinition(Lit, *R.Context) || inTemplateInstantiation(Lit, *R.Context))
      return;
    std::string Text = sourceText(Lit, *R.Context);
    if (Text.empty() || !(isdigit(Text[0]) || Text[0] == '.' || Text[0] == '+' || Text[0] == '-'))
      return;
    diag(Lit->getBeginLoc(), "Magic number %0; consider a named constant (enum, static constexpr, or #define).") << Text;
  }

private:
  static bool trivial(const Expr *Lit, ASTContext &Ctx) {
    if (const auto *FL = dyn_cast<FloatingLiteral>(Lit)) {
      double V = FL->getValueAsApproximateDouble();
      return std::floor(V / 10.0) * 10.0 == V;
    }
    const auto *IL = cast<IntegerLiteral>(Lit);
    llvm::APInt Val = IL->getValue();
    if (Val.getBitWidth() > 64 && Val.getActiveBits() > 64)
      return false;
    uint64_t V = Val.getZExtValue();
    if (V == 0 || V == 1)
      return true;
    if (llvm::isPowerOf2_64(V) && V <= 4096)
      return true;
    if (llvm::isPowerOf2_64(V + 1) && V + 1 <= (1ULL << 31)) // 3, 7, ..., 2^31-1
      return true;
    if (V == 4294967295ULL || V == 1099511627775ULL || V == 281474976710655ULL || V == 72057594037927935ULL ||
        V == 18446744073709551615ULL)
      return true;
    for (uint64_t P = 10; P <= 1000000000000ULL; P *= 10)
      if (V == P)
        return true;
    if (V % 8 == 0 && isShiftAmount(Lit, Ctx))
      return true;
    return false;
  }
  static bool isShiftAmount(const Expr *Lit, ASTContext &Ctx) {
    DynTypedNode Cur = DynTypedNode::create(*Lit);
    while (true) {
      const auto &Parents = Ctx.getParents(Cur);
      if (Parents.empty())
        return false;
      const Stmt *P = Parents[0].get<Stmt>();
      if (!P)
        return false;
      if (const auto *B = dyn_cast<BinaryOperator>(P))
        return (B->getOpcode() == BO_Shl || B->getOpcode() == BO_Shr || B->getOpcode() == BO_ShlAssign ||
                B->getOpcode() == BO_ShrAssign) &&
               B->getRHS()->IgnoreParenImpCasts() == Lit;
      if (!isa<ImplicitCastExpr>(P) && !isa<ParenExpr>(P))
        return false;
      Cur = Parents[0];
    }
  }
  static bool inConstantDefinition(const Expr *Lit, ASTContext &Ctx) {
    DynTypedNode Cur = DynTypedNode::create(*Lit);
    while (true) {
      const auto &Parents = Ctx.getParents(Cur);
      if (Parents.empty())
        return false;
      if (const auto *VD = Parents[0].get<VarDecl>())
        return !VD->isLocalVarDeclOrParm() || VD->isConstexpr() || VD->getType().isConstQualified();
      if (Parents[0].get<EnumConstantDecl>() || Parents[0].get<FieldDecl>())
        return true; // enumerator or in-class member initializer: not a local-scope variable
      if (Parents[0].get<Decl>())
        return false;
      Cur = Parents[0];
    }
  }
  static bool inTemplateInstantiation(const Expr *Lit, ASTContext &Ctx) {
    DynTypedNode Cur = DynTypedNode::create(*Lit);
    while (true) {
      const auto &Parents = Ctx.getParents(Cur);
      if (Parents.empty())
        return false;
      if (const auto *F = Parents[0].get<FunctionDecl>())
        return F->isTemplateInstantiation();
      Cur = Parents[0];
    }
  }
};

// cpp/fprime/jpl-c/multiple-stmts-per-line
class MultipleStmtsPerLineCheck : public ClangTidyCheck {
public:
  using ClangTidyCheck::ClangTidyCheck;
  void registerMatchers(MatchFinder *F) override {
    // Bodies of implicit members (defaulted copy assignment etc.) are compiler generated.
    F->addMatcher(stmt(unless(expr()), unless(hasAncestor(functionDecl(isImplicit())))).bind("parent"), this);
  }
  void check(const MatchFinder::MatchResult &R) override {
    const auto *Parent = R.Nodes.getNodeAs<Stmt>("parent");
    const SourceManager &SM = *R.SourceManager;
    llvm::SmallVector<const Stmt *, 8> Children;
    statementChildren(Parent, Children);
    std::map<std::pair<FileID, unsigned>, llvm::SmallVector<const Stmt *, 4>> ByLine;
    for (const Stmt *C : Children) {
      if (isa<CompoundStmt>(C) || isa<SwitchCase>(C) || isa<NullStmt>(C))
        continue;
      SourceLocation B = SM.getExpansionLoc(C->getBeginLoc()), E = SM.getExpansionLoc(C->getEndLoc());
      if (B.isInvalid() || E.isInvalid() || !SM.isWrittenInSameFile(B, E))
        continue;
      if (SM.getExpansionLineNumber(B) != SM.getExpansionLineNumber(E))
        continue;
      ByLine[{SM.getFileID(B), SM.getExpansionLineNumber(B)}].push_back(C);
    }
    for (auto &[Key, Stmts] : ByLine) {
      if (Stmts.size() < 2)
        continue;
      const Stmt *First = *std::min_element(Stmts.begin(), Stmts.end(), [&](const Stmt *A, const Stmt *B) {
        return SM.getExpansionColumnNumber(A->getBeginLoc()) < SM.getExpansionColumnNumber(B->getBeginLoc());
      });
      diag(First->getBeginLoc(), "This line contains %0 statements; only one is allowed.") << (unsigned)Stmts.size();
    }
  }

private:
  static void statementChildren(const Stmt *P, llvm::SmallVectorImpl<const Stmt *> &Out) {
    if (const auto *CS = dyn_cast<CompoundStmt>(P)) {
      Out.append(CS->body_begin(), CS->body_end());
    } else if (const auto *If = dyn_cast<IfStmt>(P)) {
      Out.push_back(If->getThen());
      if (If->getElse())
        Out.push_back(If->getElse());
    } else if (const auto *F = dyn_cast<ForStmt>(P)) {
      Out.push_back(F->getBody());
    } else if (const auto *W = dyn_cast<WhileStmt>(P)) {
      Out.push_back(W->getBody());
    } else if (const auto *D = dyn_cast<DoStmt>(P)) {
      Out.push_back(D->getBody());
    } else if (const auto *S = dyn_cast<SwitchStmt>(P)) {
      Out.push_back(S->getBody());
    } else if (const auto *C = dyn_cast<SwitchCase>(P)) {
      if (C->getSubStmt())
        Out.push_back(C->getSubStmt());
    } else if (const auto *L = dyn_cast<LabelStmt>(P)) {
      Out.push_back(L->getSubStmt());
    } else if (const auto *RF = dyn_cast<CXXForRangeStmt>(P)) {
      Out.push_back(RF->getBody());
    }
  }
};

// cpp/fprime/for-header-flow-control
class ForHeaderFlowControlCheck : public ClangTidyCheck {
public:
  using ClangTidyCheck::ClangTidyCheck;
  void registerMatchers(MatchFinder *F) override { F->addMatcher(forStmt().bind("for"), this); }
  void check(const MatchFinder::MatchResult &R) override {
    const auto *For = R.Nodes.getNodeAs<ForStmt>("for");
    std::set<const ValueDecl *> CondVars;
    llvm::SmallVector<const Expr *, 8> CondExprs;
    collect<Expr>(For->getCond(), CondExprs);
    for (const Expr *E : CondExprs)
      if (const ValueDecl *V = accessedDecl(E))
        CondVars.insert(V);
    llvm::SmallVector<const Expr *, 16> Header;
    if (const auto *InitExpr = dyn_cast_or_null<Expr>(For->getInit()))
      collect<Expr>(InitExpr, Header);
    collect<Expr>(For->getInc(), Header);
    for (const Expr *E : Header) {
      if (const auto *Call = dyn_cast<CallExpr>(E)) {
        const FunctionDecl *Callee = Call->getDirectCallee();
        if (!Callee)
          continue;
        if (isIteratorCrement(Call, CondVars))
          continue;
        diag(Call->getBeginLoc(), "This for-statement header expression calls the function %0.") << Callee->getNameAsString();
      } else if (const ValueDecl *V = mutatedVariable(E); V && !CondVars.count(V)) {
        diag(E->getBeginLoc(), "This for-statement header expression modifies the non-loop-control variable %0.") << V->getName();
      }
    }
  }

private:
  static bool isIteratorCrement(const CallExpr *Call, const std::set<const ValueDecl *> &CondVars) {
    const auto *OC = dyn_cast<CXXOperatorCallExpr>(Call);
    if (!OC || (OC->getOperator() != OO_PlusPlus && OC->getOperator() != OO_MinusMinus) || OC->getNumArgs() == 0)
      return false;
    const ValueDecl *V = accessedDecl(OC->getArg(0)->IgnoreParenImpCasts());
    return V && CondVars.count(V);
  }
  static const ValueDecl *mutatedVariable(const Expr *E) {
    if (const auto *B = dyn_cast<BinaryOperator>(E); B && B->isAssignmentOp())
      return accessedDecl(B->getLHS()->IgnoreParenImpCasts());
    if (const auto *U = dyn_cast<UnaryOperator>(E); U && U->isIncrementDecrementOp())
      return accessedDecl(U->getSubExpr()->IgnoreParenImpCasts());
    return nullptr;
  }
};

// cpp/fprime/jpl-c/basic-int-types
//
// Candidates are collected while the TU is traversed; the "external API" exclusion
// (values that come from or go to declarations outside the project) needs the whole
// TU, so the verdicts are computed in onEndOfTranslationUnit.
class BasicIntTypesCheck : public ClangTidyCheck {
public:
  using ClangTidyCheck::ClangTidyCheck;
  void registerMatchers(MatchFinder *F) override {
    F->addMatcher(varDecl(unless(isImplicit())).bind("var"), this);
    F->addMatcher(functionDecl(isDefinition(), unless(isImplicit())).bind("fn"), this);
  }
  void check(const MatchFinder::MatchResult &R) override {
    Ctx = R.Context;
    const SourceManager &SM = *R.SourceManager;
    const NamedDecl *D = nullptr;
    QualType T;
    if (const auto *V = R.Nodes.getNodeAs<VarDecl>("var")) {
      D = V;
      T = V->getType();
    } else {
      const auto *Fn = R.Nodes.getNodeAs<FunctionDecl>("fn");
      D = Fn;
      T = Fn->getReturnType();
    }
    if (D->getLocation().isInvalid() || SM.isInSystemHeader(D->getLocation()) || T.isNull())
      return;
    StringRef Path = SM.getFilename(SM.getExpansionLoc(D->getLocation()));
    if (Path.contains("Utils/Hash/libcrc/") || Path.contains("fprime-venv/"))
      return;
    if (const auto *Fn = dyn_cast<FunctionDecl>(D); Fn && Fn->isTemplateInstantiation())
      return;
    if (const auto *V = dyn_cast<VarDecl>(D)) {
      const auto *Fn = dyn_cast_or_null<FunctionDecl>(V->getDeclContext());
      if (Fn && Fn->isTemplateInstantiation())
        return;
      if (const auto *P = dyn_cast<ParmVarDecl>(V)) {
        if (Fn && Fn->isOverloadedOperator() &&
            (Fn->getOverloadedOperator() == OO_PlusPlus || Fn->getOverloadedOperator() == OO_MinusMinus))
          return;
        // One report per parameter, at the definition (CodeQL's Parameter is one entity).
        if (!Fn || !Fn->doesThisDeclarationHaveABody())
          return;
      }
    }
    const BuiltinType *Used = nullptr;
    if (!usesBasicInt(T, SM, Used, 0))
      return;
    if (Used->getKind() == BuiltinType::Char_S || Used->getKind() == BuiltinType::Char_U)
      if (isPlainCharIndirection(T))
        return;
    Candidates.push_back({D, Used});
  }

  void onEndOfTranslationUnit() override {
    if (!Ctx || Candidates.empty()) {
      Candidates.clear();
      return;
    }
    ExternalApi External(*Ctx);
    for (auto [D, Used] : Candidates) {
      if (External.isExternal(D))
        continue;
      diag(D->getLocation(), "%0 uses the basic integral type %1 rather than a typedef with size and signedness.")
          << D->getName() << QualType(Used, 0).getAsString();
    }
    Candidates.clear();
    Ctx = nullptr;
  }

private:
  llvm::SmallVector<std::pair<const NamedDecl *, const BuiltinType *>, 32> Candidates;
  ASTContext *Ctx = nullptr;

  static bool allowedTypedef(StringRef Name) {
    static const char *Allowed[] = {"I64", "U64", "I32", "U32", "I16", "U16", "I8", "U8", "F64", "F32", "int64_t", "uint64_t",
                                    "int32_t", "uint32_t", "int16_t", "uint16_t", "int8_t", "uint8_t"};
    return llvm::is_contained(Allowed, Name);
  }
  // Walk typedefs (except allowed/system ones) and derived types down to a builtin integer.
  static bool usesBasicInt(QualType T, const SourceManager &SM, const BuiltinType *&Used, unsigned Depth) {
    if (T.isNull() || Depth > 16)
      return false;
    const Type *Ty = T.getTypePtr();
    if (const auto *TT = dyn_cast<TypedefType>(Ty)) {
      const TypedefNameDecl *TD = TT->getDecl();
      if (allowedTypedef(TD->getName()) || SM.isInSystemHeader(TD->getLocation()))
        return false;
      return usesBasicInt(TD->getUnderlyingType(), SM, Used, Depth + 1);
    }
    if (const auto *ET = dyn_cast<ElaboratedType>(Ty))
      return usesBasicInt(ET->getNamedType(), SM, Used, Depth + 1);
    if (const auto *BT = dyn_cast<BuiltinType>(Ty)) {
      if (BT->isInteger() && BT->getKind() != BuiltinType::Bool && BT->getKind() != BuiltinType::WChar_S &&
          BT->getKind() != BuiltinType::WChar_U && BT->getKind() != BuiltinType::Char16 && BT->getKind() != BuiltinType::Char32) {
        Used = BT;
        return true;
      }
      return false;
    }
    if (const auto *PT = dyn_cast<PointerType>(Ty))
      return usesBasicInt(PT->getPointeeType(), SM, Used, Depth + 1);
    if (const auto *RT = dyn_cast<ReferenceType>(Ty))
      return usesBasicInt(RT->getPointeeType(), SM, Used, Depth + 1);
    if (const auto *AT = dyn_cast<ArrayType>(Ty))
      return usesBasicInt(AT->getElementType(), SM, Used, Depth + 1);
    if (const auto *PT = dyn_cast<ParenType>(Ty))
      return usesBasicInt(PT->getInnerType(), SM, Used, Depth + 1);
    if (const auto *AT = dyn_cast<AttributedType>(Ty))
      return usesBasicInt(AT->getModifiedType(), SM, Used, Depth + 1);
    if (const auto *DT = dyn_cast<DecayedType>(Ty))
      return usesBasicInt(DT->getOriginalType(), SM, Used, Depth + 1);
    return false;
  }
  static bool isPlainCharIndirection(QualType T) {
    const Type *Ty = T.getTypePtr();
    QualType Base;
    if (const auto *PT = dyn_cast<PointerType>(Ty))
      Base = PT->getPointeeType();
    else if (const auto *RT = dyn_cast<ReferenceType>(Ty))
      Base = RT->getPointeeType();
    else if (const auto *AT = dyn_cast<ArrayType>(Ty))
      Base = AT->getElementType();
    else
      return false;
    if (const auto *BT = Base->getAs<BuiltinType>())
      if (BT->getKind() == BuiltinType::Char_S || BT->getKind() == BuiltinType::Char_U)
        return true;
    return isPlainCharIndirection(Base);
  }

  // CodeQL's externalApiDeclaration, computed as a fixpoint over the translation unit.
  class ExternalApi {
  public:
    explicit ExternalApi(ASTContext &Ctx) : Ctx(Ctx), SM(Ctx.getSourceManager()) {
      gather(Ctx.getTranslationUnitDecl());
      seed();
      for (unsigned Iter = 0; Iter < 8 && propagate(); ++Iter) {
      }
    }
    bool isExternal(const NamedDecl *D) const { return External.count(key(D)); }

  private:
    ASTContext &Ctx;
    const SourceManager &SM;
    llvm::SmallVector<const CallExpr *, 256> Calls;
    llvm::SmallVector<const BinaryOperator *, 256> Ops;
    llvm::SmallVector<const ReturnStmt *, 128> Returns;
    llvm::SmallVector<const VarDecl *, 256> Vars;
    std::set<const Decl *> External;

    // Parameters are one entity across declaration and definition.
    static const Decl *key(const NamedDecl *D) {
      if (const auto *P = dyn_cast<ParmVarDecl>(D))
        if (const auto *Fn = dyn_cast_or_null<FunctionDecl>(P->getDeclContext()))
          return Fn->getCanonicalDecl()->getParamDecl(P->getFunctionScopeIndex());
      if (const auto *Fn = dyn_cast<FunctionDecl>(D))
        return Fn->getCanonicalDecl();
      return D->getCanonicalDecl();
    }
    void gather(const Decl *D) {
      if (const auto *DC = dyn_cast<DeclContext>(D))
        for (const Decl *Child : DC->decls())
          gather(Child);
      if (const auto *FD = dyn_cast<FunctionDecl>(D); FD && FD->doesThisDeclarationHaveABody()) {
        collect<CallExpr>(FD->getBody(), Calls);
        collect<BinaryOperator>(FD->getBody(), Ops);
        collect<ReturnStmt>(FD->getBody(), Returns);
        gatherVars(FD->getBody());
        for (const ParmVarDecl *P : FD->parameters())
          Vars.push_back(P);
      } else if (const auto *VD = dyn_cast<VarDecl>(D); VD && !isa<ParmVarDecl>(VD)) {
        Vars.push_back(VD);
        if (VD->getInit())
          collect<CallExpr>(VD->getInit(), Calls);
      }
    }
    void gatherVars(const Stmt *S) {
      if (!S)
        return;
      if (const auto *DS = dyn_cast<DeclStmt>(S))
        for (const Decl *D : DS->decls())
          if (const auto *VD = dyn_cast<VarDecl>(D))
            Vars.push_back(VD);
      for (const Stmt *C : S->children())
        gatherVars(C);
    }
    bool isExternalFunction(const FunctionDecl *F) const { return F && SM.isInSystemHeader(F->getLocation()); }
    bool externalValue(const Expr *E) const {
      if (!E)
        return false;
      E = E->IgnoreParenImpCasts();
      if (const auto *C = dyn_cast<CallExpr>(E))
        return isExternalFunction(C->getDirectCallee());
      if (E->getBeginLoc().isMacroID() && SM.isInSystemHeader(SM.getSpellingLoc(E->getBeginLoc())))
        return true; // errno, INADDR_ANY, ...
      if (const auto *ME = dyn_cast<MemberExpr>(E))
        return SM.isInSystemHeader(ME->getMemberDecl()->getLocation());
      return false;
    }
    llvm::SmallVector<const Expr *, 4> assignedExprs(const VarDecl *V) const {
      llvm::SmallVector<const Expr *, 4> Out;
      if (V->getInit())
        Out.push_back(V->getInit());
      for (const BinaryOperator *B : Ops)
        if (B->getOpcode() == BO_Assign && accessedDecl(B->getLHS()->IgnoreParenImpCasts()) == V)
          Out.push_back(B->getRHS());
      return Out;
    }
    bool carriesExternal(const Expr *E) const {
      if (externalValue(E))
        return true;
      if (const auto *V = dyn_cast_or_null<VarDecl>(accessedDecl(E->IgnoreParenImpCasts())))
        for (const Expr *A : assignedExprs(V))
          if (externalValue(A))
            return true;
      return false;
    }
    static const Expr *stripAddrOf(const Expr *E) {
      E = E->IgnoreParenImpCasts();
      if (const auto *U = dyn_cast<UnaryOperator>(E); U && U->getOpcode() == UO_AddrOf)
        return U->getSubExpr()->IgnoreParenImpCasts();
      return E;
    }
    void seed() {
      for (const VarDecl *V : Vars) {
        for (const Expr *A : assignedExprs(V))
          if (externalValue(A) || (V->getInit() == A && A->getBeginLoc().isMacroID() &&
                                   SM.isInSystemHeader(SM.getSpellingLoc(A->getBeginLoc()))))
            External.insert(key(V));
        for (const BinaryOperator *B : Ops)
          if (B->isComparisonOp()) {
            bool Mine = accessedDecl(B->getLHS()->IgnoreParenImpCasts()) == V ||
                        accessedDecl(B->getRHS()->IgnoreParenImpCasts()) == V;
            if (Mine && (carriesExternal(B->getLHS()) || carriesExternal(B->getRHS())))
              External.insert(key(V));
          }
      }
      for (const CallExpr *C : Calls) {
        const FunctionDecl *Callee = C->getDirectCallee();
        if (!Callee)
          continue;
        if (isExternalFunction(Callee))
          for (const Expr *Arg : C->arguments())
            if (const auto *V = dyn_cast_or_null<VarDecl>(accessedDecl(stripAddrOf(Arg))))
              External.insert(key(V));
        for (unsigned I = 0; I < C->getNumArgs() && I < Callee->getNumParams(); ++I)
          if (carriesExternal(C->getArg(I)))
            External.insert(key(Callee->getParamDecl(I)));
      }
      for (const ReturnStmt *RS : Returns)
        if (RS->getRetValue() && carriesExternal(RS->getRetValue()))
          if (const FunctionDecl *F = enclosingFunction(RS, Ctx))
            External.insert(key(F));
    }
    bool propagate() {
      size_t Before = External.size();
      for (const VarDecl *V : Vars) {
        if (External.count(key(V)))
          continue;
        bool Ext = false;
        for (const BinaryOperator *B : Ops) {
          if (B->getOpcode() == BO_Assign && accessedDecl(B->getRHS()->IgnoreParenImpCasts()) == V)
            if (const auto *V2 = dyn_cast_or_null<VarDecl>(accessedDecl(B->getLHS()->IgnoreParenImpCasts()));
                V2 && External.count(key(V2)))
              Ext = true;
          if (B->isComparisonOp()) {
            const ValueDecl *L = accessedDecl(B->getLHS()->IgnoreParenImpCasts());
            const ValueDecl *Rr = accessedDecl(B->getRHS()->IgnoreParenImpCasts());
            const ValueDecl *Other = L == V ? Rr : Rr == V ? L : nullptr;
            if (Other && Other != V && isa<VarDecl>(Other) && External.count(key(cast<VarDecl>(Other))))
              Ext = true;
          }
        }
        for (const VarDecl *V2 : Vars)
          if (V2 != V && External.count(key(V2)) && V2->getInit() &&
              accessedDecl(V2->getInit()->IgnoreParenImpCasts()) == V)
            Ext = true;
        if (!Ext)
          for (const CallExpr *C : Calls) {
            const FunctionDecl *Callee = C->getDirectCallee();
            if (!Callee)
              continue;
            for (unsigned I = 0; I < C->getNumArgs() && I < Callee->getNumParams(); ++I)
              if (accessedDecl(stripAddrOf(C->getArg(I))) == V && External.count(key(Callee->getParamDecl(I))))
                Ext = true;
          }
        if (Ext)
          External.insert(key(V));
      }
      return External.size() != Before;
    }
  };
};

// cpp/fprime/jpl-c/non-const-function-pointer
class NonConstFunctionPointerCheck : public ClangTidyCheck {
public:
  using ClangTidyCheck::ClangTidyCheck;
  void registerMatchers(MatchFinder *F) override {
    F->addMatcher(callExpr(unless(cxxOperatorCallExpr()), unless(isTypeDependent())).bind("call"), this);
  }
  void check(const MatchFinder::MatchResult &R) override {
    const auto *Call = R.Nodes.getNodeAs<CallExpr>("call");
    const Expr *Callee = Call->getCallee()->IgnoreParenImpCasts();
    if (isa<CXXMemberCallExpr>(Call)) {
      const auto *BO = dyn_cast<BinaryOperator>(Callee);
      if (!BO || !BO->isPtrMemOp())
        return; // ordinary member call
    }
    // A function-pointer template argument is still a call through a pointer in the instantiation.
    bool Substituted = isa<SubstNonTypeTemplateParmExpr>(Call->getCallee()->IgnoreParens()) ||
                       isa<SubstNonTypeTemplateParmExpr>(Callee);
    if (Call->getDirectCallee() && !Substituted)
      return; // direct call to a known function
    if (isa<CXXPseudoDestructorExpr>(Callee) || isa<UnresolvedLookupExpr>(Callee) || Callee->getType().isNull())
      return;
    QualType T = Callee->getType();
    if (const auto *BO = dyn_cast<BinaryOperator>(Callee); BO && BO->isPtrMemOp())
      T = BO->getRHS()->getType(); // (obj->*member_fn_ptr)(...): the pointer is the RHS
    if (!Substituted && !T->isFunctionPointerType() && !T->isFunctionType() && !T->isFunctionReferenceType() &&
        !T->isMemberFunctionPointerType())
      return;
    if (T.isConstQualified())
      return;
    diag(Call->getBeginLoc(), "This call does not go through a const function pointer.");
  }
};

// cpp/fprime/jpl-c/preprocessor-use-ifdef
class PreprocessorUseIfdefCheck : public ClangTidyCheck {
  class Callbacks : public PPCallbacks {
  public:
    Callbacks(PreprocessorUseIfdefCheck &C, const SourceManager &SM, const LangOptions &LO) : Check(C), SM(SM), LO(LO) {}
    void MacroDefined(const Token &Name, const MacroDirective *MD) override {
      const MacroInfo *MI = MD->getMacroInfo();
      if (MI && !MI->isBuiltinMacro() && MI->getDefinitionLoc().isValid() && !SM.isInSystemHeader(MI->getDefinitionLoc()))
        RepoMacros.insert(Name.getIdentifierInfo()->getName().str());
    }
    void If(SourceLocation Loc, SourceRange ConditionRange, ConditionValueKind) override {
      StringRef Text = Lexer::getSourceText(CharSourceRange::getTokenRange(ConditionRange), SM, LO);
      report(Loc, Text.str());
    }
    void Ifdef(SourceLocation Loc, const Token &MacroName, const MacroDefinition &) override {
      report(Loc, MacroName.getIdentifierInfo() ? MacroName.getIdentifierInfo()->getName().str() : "");
    }
    void Ifndef(SourceLocation Loc, const Token &MacroName, const MacroDefinition &) override {
      report(Loc, MacroName.getIdentifierInfo() ? MacroName.getIdentifierInfo()->getName().str() : "");
    }

  private:
    void report(SourceLocation Loc, const std::string &Condition) {
      if (!fromSource(Loc, SM))
        return;
      StringRef Path = SM.getFilename(Loc);
      if (isHeaderPath(Path))
        return;
      std::vector<std::string> Ids;
      static const std::regex Ident("[A-Za-z_][A-Za-z0-9_]*");
      for (std::sregex_iterator It(Condition.begin(), Condition.end(), Ident), End; It != End; ++It)
        if (It->str() != "defined")
          Ids.push_back(It->str());
      bool OsPlatform = Path.contains("Os/Posix/") || Path.contains("Os/Linux/") || Path.contains("Os/Darwin/");
      if (OsPlatform && !Ids.empty() && llvm::any_of(Ids, [&](const std::string &Id) { return !RepoMacros.count(Id); }))
        return; // external feature detection in the platform layer
      bool FwPackage = Path.contains("/Fw/") || Path.contains("/Os/") || Path.startswith("Fw/") || Path.startswith("Os/");
      if (FwPackage && !Ids.empty() &&
          llvm::all_of(Ids, [](const std::string &Id) { return StringRef(Id).startswith("FW_") || Id == "BUILD_UT"; }))
        return; // FpConfig switch
      Check.diag(Loc, "Use of conditional compilation must be kept to a minimum.");
    }
    PreprocessorUseIfdefCheck &Check;
    const SourceManager &SM;
    const LangOptions &LO;
    std::set<std::string> RepoMacros;
  };

public:
  using ClangTidyCheck::ClangTidyCheck;
  void registerPPCallbacks(const SourceManager &SM, Preprocessor *PP, Preprocessor *) override {
    PP->addPPCallbacks(std::make_unique<Callbacks>(*this, SM, PP->getLangOpts()));
  }
};

// cpp/fprime/float-narrowing
class FloatNarrowingCheck : public ClangTidyCheck {
public:
  using ClangTidyCheck::ClangTidyCheck;
  void registerMatchers(MatchFinder *F) override {
    F->addMatcher(implicitCastExpr(hasCastKind(CK_FloatingCast)).bind("cast"), this);
  }
  void check(const MatchFinder::MatchResult &R) override {
    const auto *Cast = R.Nodes.getNodeAs<ImplicitCastExpr>("cast");
    if (Cast->isPartOfExplicitCast())
      return;
    const Expr *Sub = Cast->getSubExpr();
    QualType Src = Sub->getType(), Dst = Cast->getType();
    if (Src->isDependentType() || Dst->isDependentType())
      return;
    if (R.Context->getTypeSize(Dst) >= R.Context->getTypeSize(Src))
      return;
    if (isa<FloatingLiteral>(Sub->IgnoreParens()) || isConstantValue(Sub, *R.Context))
      return;
    diag(Sub->getBeginLoc(), "Value of type %0 is implicitly narrowed to %1.")
        << Src.getCanonicalType().getUnqualifiedType().getAsString() << Dst.getCanonicalType().getUnqualifiedType().getAsString();
  }
};

// cpp/fprime/jpl-c/no-boolean-side-effects
class NoBooleanSideEffectsCheck : public ClangTidyCheck {
public:
  using ClangTidyCheck::ClangTidyCheck;
  void registerMatchers(MatchFinder *F) override {
    F->addMatcher(ifStmt(hasCondition(expr().bind("cond"))), this);
    F->addMatcher(forStmt(hasCondition(expr().bind("cond"))), this);
    F->addMatcher(whileStmt(hasCondition(expr().bind("cond"))), this);
    F->addMatcher(doStmt(hasCondition(expr().bind("cond"))), this);
    F->addMatcher(conditionalOperator(hasCondition(expr().bind("cond"))), this);
  }
  void check(const MatchFinder::MatchResult &R) override {
    const auto *Cond = R.Nodes.getNodeAs<Expr>("cond");
    if (Cond->isInstantiationDependent())
      return;
    if (hasSideEffect(Cond))
      diag(Cond->getBeginLoc(), "This Boolean expression is not side-effect free.");
  }

private:
  // Memoised, cycle-safe recursion over the call graph (CodeQL's least-fixpoint semantics:
  // a function is only unsafe/impure when that is derivable, so cycles contribute false).
  std::map<const FunctionDecl *, int> UnsafeMemo, ImpureMemo; // 0 in progress, 1 true, 2 false

  bool hasSideEffect(const Stmt *S) {
    if (!S)
      return false;
    if (const auto *B = dyn_cast<BinaryOperator>(S); B && B->isAssignmentOp())
      return true;
    if (const auto *U = dyn_cast<UnaryOperator>(S); U && U->isIncrementDecrementOp())
      return true;
    if (const auto *OC = dyn_cast<CXXOperatorCallExpr>(S); OC && (OC->isAssignmentOp() || OC->getOperator() == OO_PlusPlus || OC->getOperator() == OO_MinusMinus))
      return true;
    if (const auto *C = dyn_cast<CallExpr>(S); C && !isa<CXXOperatorCallExpr>(C)) {
      const FunctionDecl *F = C->getDirectCallee();
      if (!F)
        return true; // indirect call
      if (!F->isConstexpr() && F->getQualifiedNameAsString() != "Fw::SwAssert" && (inherentlyUnsafe(F) || !safeToCall(F)) &&
          maybeImpure(F))
        return true;
    }
    for (const Stmt *Child : S->children())
      if (hasSideEffect(Child))
        return true;
    return false;
  }
  static bool pointsToNonConst(QualType T, unsigned Depth) {
    if (Depth > 6)
      return false;
    T = T.getNonReferenceType();
    if (const auto *PT = T->getAs<PointerType>())
      return !PT->getPointeeType().isConstQualified() || pointsToNonConst(PT->getPointeeType(), Depth + 1);
    if (const auto *AT = T->getAsArrayTypeUnsafe())
      return pointsToNonConst(AT->getElementType(), Depth + 1);
    if (const auto *RD = T->getAsCXXRecordDecl(); RD && RD->hasDefinition())
      for (const FieldDecl *FD : RD->fields())
        if (pointsToNonConst(FD->getType(), Depth + 1))
          return true;
    return false;
  }
  bool safeToCall(const FunctionDecl *F) {
    for (const ParmVarDecl *P : F->parameters())
      if (pointsToNonConst(P->getType(), 0))
        return false;
    return !inherentlyUnsafe(F);
  }
  static bool writesGlobal(const Stmt *S) {
    if (!S)
      return false;
    const Expr *Target = nullptr;
    if (const auto *B = dyn_cast<BinaryOperator>(S); B && B->isAssignmentOp())
      Target = B->getLHS();
    else if (const auto *U = dyn_cast<UnaryOperator>(S); U && U->isIncrementDecrementOp())
      Target = U->getSubExpr();
    if (Target)
      if (const auto *VD = dyn_cast_or_null<VarDecl>(accessedDecl(Target->IgnoreParenImpCasts())))
        if (VD->hasGlobalStorage())
          return true;
    for (const Stmt *C : S->children())
      if (writesGlobal(C))
        return true;
    return false;
  }
  bool inherentlyUnsafe(const FunctionDecl *F) {
    F = F->getCanonicalDecl();
    auto It = UnsafeMemo.find(F);
    if (It != UnsafeMemo.end())
      return It->second == 1;
    UnsafeMemo[F] = 0;
    bool Result = false;
    const FunctionDecl *Def = nullptr;
    if (F->hasBody(Def) && Def) {
      if (writesGlobal(Def->getBody()))
        Result = true;
      else {
        llvm::SmallVector<const CallExpr *, 8> Calls;
        collect<CallExpr>(Def->getBody(), Calls);
        for (const CallExpr *C : Calls)
          if (const FunctionDecl *Callee = C->getDirectCallee(); Callee && inherentlyUnsafe(Callee)) {
            Result = true;
            break;
          }
      }
    }
    UnsafeMemo[F] = Result ? 1 : 2;
    return Result;
  }
  static bool writesNonLocal(const Stmt *S) {
    if (!S)
      return false;
    const Expr *Target = nullptr;
    if (const auto *B = dyn_cast<BinaryOperator>(S); B && B->isAssignmentOp())
      Target = B->getLHS();
    else if (const auto *U = dyn_cast<UnaryOperator>(S); U && U->isIncrementDecrementOp())
      Target = U->getSubExpr();
    if (Target) {
      const auto *VD = dyn_cast_or_null<VarDecl>(accessedDecl(Target->IgnoreParenImpCasts()));
      bool Local = VD && VD->isLocalVarDeclOrParm() && !VD->getType()->isReferenceType();
      if (!Local)
        return true;
    }
    for (const Stmt *C : S->children())
      if (writesNonLocal(C))
        return true;
    return false;
  }
  bool maybeImpure(const FunctionDecl *F) {
    F = F->getCanonicalDecl();
    const FunctionDecl *Def = nullptr;
    if (!F->hasBody(Def) || !Def)
      return true;
    auto It = ImpureMemo.find(F);
    if (It != ImpureMemo.end())
      return It->second == 1;
    ImpureMemo[F] = 0;
    bool Result = writesNonLocal(Def->getBody());
    if (!Result) {
      llvm::SmallVector<const Stmt *, 32> Nodes;
      collect<Stmt>(Def->getBody(), Nodes);
      for (const Stmt *N : Nodes) {
        if (isa<CXXNewExpr>(N) || isa<CXXDeleteExpr>(N) || isa<AsmStmt>(N)) {
          Result = true;
          break;
        }
        if (const auto *C = dyn_cast<CallExpr>(N)) {
          const FunctionDecl *Callee = C->getDirectCallee();
          if (!Callee) {
            Result = true;
            break;
          }
          if (Callee->isConstexpr() || Callee->getQualifiedNameAsString() == "Fw::SwAssert")
            continue;
          if (maybeImpure(Callee) && (Callee->hasBody() || !safeCallTarget(Callee))) {
            Result = true;
            break;
          }
        }
      }
    }
    ImpureMemo[F] = Result ? 1 : 2;
    return Result;
  }
  bool safeCallTarget(const FunctionDecl *F) {
    if (!safeToCall(F))
      return false;
    if (const auto *M = dyn_cast<CXXMethodDecl>(F)) {
      if (M->isVirtual())
        return false;
      if (!M->isStatic() && !M->isConst())
        return false;
    }
    for (const ParmVarDecl *P : F->parameters())
      if (const auto *RT = P->getType()->getAs<ReferenceType>(); RT && !RT->getPointeeType().isConstQualified())
        return false;
    return true;
  }
};

void registerStyleChecks(ClangTidyCheckFactories &F) {
  F.registerCheck<MagicNumbersCheck>("fprime-magic-numbers");
  F.registerCheck<MultipleStmtsPerLineCheck>("fprime-multiple-stmts-per-line");
  F.registerCheck<ForHeaderFlowControlCheck>("fprime-for-header-flow-control");
  F.registerCheck<BasicIntTypesCheck>("fprime-basic-int-types");
  F.registerCheck<NonConstFunctionPointerCheck>("fprime-non-const-function-pointer");
  F.registerCheck<PreprocessorUseIfdefCheck>("fprime-preprocessor-use-ifdef");
  F.registerCheck<FloatNarrowingCheck>("fprime-float-narrowing");
  F.registerCheck<NoBooleanSideEffectsCheck>("fprime-no-boolean-side-effects");
}

} // namespace fprime
} // namespace tidy
} // namespace clang
