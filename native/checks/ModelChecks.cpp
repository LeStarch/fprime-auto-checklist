// Checks that reason about the F Prime component model: constructors, command
// handlers, cyclic handlers and ground-originated data.
#include "FprimeChecks.h"

#include "clang/AST/ParentMapContext.h"
#include "clang/Analysis/CFG.h"

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

bool containsStmt(const Stmt *Outer, const Stmt *Inner) {
  if (!Outer)
    return false;
  if (Outer == Inner)
    return true;
  for (const Stmt *C : Outer->children())
    if (containsStmt(C, Inner))
      return true;
  return false;
}

StringRef typedefName(QualType T) {
  T = T.getNonReferenceType().getLocalUnqualifiedType();
  if (const auto *TT = T->getAs<TypedefType>())
    return TT->getDecl()->getName();
  return StringRef();
}

bool isCmdHandler(const CXXMethodDecl *M) {
  return M && M->getDeclName().isIdentifier() && M->getName().endswith("cmdHandler") && M->getNumParams() >= 2 &&
         typedefName(M->getParamDecl(0)->getType()) == "FwOpcodeType" &&
         typedefName(M->getParamDecl(1)->getType()) == "U32" && M->hasBody();
}

const ParmVarDecl *paramOf(const Expr *E) {
  return dyn_cast_or_null<ParmVarDecl>(accessedDecl(E->IgnoreParenImpCasts()));
}
const FieldDecl *fieldOf(const Expr *E) {
  return dyn_cast_or_null<FieldDecl>(accessedDecl(E->IgnoreParenImpCasts()));
}

} // namespace

// cpp/fprime/uninitialized-member
class UninitializedMemberCheck : public ClangTidyCheck {
public:
  using ClangTidyCheck::ClangTidyCheck;
  void registerMatchers(MatchFinder *F) override {
    F->addMatcher(cxxConstructorDecl(isDefinition(), unless(isImplicit()), unless(isDeleted()), unless(isCopyConstructor()),
                                     unless(isMoveConstructor()), hasBody(stmt()))
                      .bind("ctor"),
                  this);
  }
  void check(const MatchFinder::MatchResult &R) override {
    const auto *Ctor = R.Nodes.getNodeAs<CXXConstructorDecl>("ctor");
    const CXXRecordDecl *Class = Ctor->getParent();
    if (Class->isUnion() || Ctor->isTemplateInstantiation())
      return;
    for (const FieldDecl *M : Class->fields()) {
      if (!isScalarLike(M->getType()) || M->hasInClassInitializer())
        continue;
      bool Initialised = false;
      for (const CXXCtorInitializer *Init : Ctor->inits())
        if (Init->isMemberInitializer() && Init->getMember() == M && Init->isWritten())
          Initialised = true;
      if (Initialised || writtenIn(M, Ctor, 0))
        continue;
      diag(Ctor->getLocation(), "Constructor of %0 does not initialize member %1.") << Class->getName() << M->getName();
    }
  }

private:
  static bool isScalarLike(QualType T) {
    while (const auto *Arr = T->getAsArrayTypeUnsafe())
      T = Arr->getElementType();
    return T->isArithmeticType() || T->isPointerType() || T->isEnumeralType();
  }
  static bool writtenIn(const FieldDecl *M, const FunctionDecl *F, unsigned Depth) {
    if (!F || !F->hasBody() || Depth > 4)
      return false;
    llvm::SmallVector<const MemberExpr *, 8> Accesses;
    collect<MemberExpr>(F->getBody(), Accesses);
    if (const auto *Ctor = dyn_cast<CXXConstructorDecl>(F))
      for (const CXXCtorInitializer *Init : Ctor->inits())
        collect<MemberExpr>(Init->getInit(), Accesses);
    ASTContext &Ctx = F->getASTContext();
    for (const MemberExpr *ME : Accesses) {
      if (ME->getMemberDecl() != M)
        continue;
      if (modifiedAccess(ME, Ctx))
        return true;
    }
    llvm::SmallVector<const CallExpr *, 8> Calls;
    collect<CallExpr>(F->getBody(), Calls);
    for (const CallExpr *C : Calls)
      if (const FunctionDecl *Callee = C->getDirectCallee(); Callee && Callee != F && writtenIn(M, Callee, Depth + 1))
        return true;
    return false;
  }
  // The access is modified, has its address taken, sits under an assignment, or (array) is passed to a call.
  static bool modifiedAccess(const MemberExpr *ME, ASTContext &Ctx) {
    DynTypedNode Cur = DynTypedNode::create(*ME);
    bool First = true;
    while (true) {
      const auto &Parents = Ctx.getParents(Cur);
      if (Parents.empty())
        return false;
      const Stmt *P = Parents[0].get<Stmt>();
      if (!P)
        return false;
      if (const auto *B = dyn_cast<BinaryOperator>(P)) {
        if (B->isAssignmentOp())
          return true;
      } else if (const auto *OC = dyn_cast<CXXOperatorCallExpr>(P)) {
        if (OC->isAssignmentOp() || OC->getOperator() == OO_PlusPlus || OC->getOperator() == OO_MinusMinus)
          return true;
      } else if (const auto *U = dyn_cast<UnaryOperator>(P)) {
        if (First && (U->isIncrementDecrementOp() || U->getOpcode() == UO_AddrOf))
          return true;
      } else if (const auto *CE = dyn_cast<CXXConstructExpr>(P)) {
        if (ME->getType()->isArrayType())
          for (const Expr *Arg : CE->arguments())
            if (Arg->IgnoreParenImpCasts() == ME)
              return true;
        return false;
      } else if (const auto *C = dyn_cast<CallExpr>(P)) {
        if (ME->getType()->isArrayType())
          for (const Expr *Arg : C->arguments())
            if (Arg->IgnoreParenImpCasts() == ME)
              return true;
        if (const auto *MC = dyn_cast<CXXMemberCallExpr>(C))
          if (MC->getImplicitObjectArgument()->IgnoreParenImpCasts() == ME && !MC->getMethodDecl()->isConst())
            return true;
        return false;
      } else if (const auto *AE = dyn_cast<ArraySubscriptExpr>(P)) {
        if (AE->getBase()->IgnoreParenImpCasts() != ME)
          return false;
      } else if (!isa<ImplicitCastExpr>(P) && !isa<ParenExpr>(P) && !isa<MemberExpr>(P)) {
        return false;
      }
      First = false;
      Cur = Parents[0];
    }
  }
};

// cpp/fprime/unthrottled-cyclic-event
class UnthrottledCyclicEventCheck : public ClangTidyCheck {
public:
  using ClangTidyCheck::ClangTidyCheck;
  void registerMatchers(MatchFinder *F) override {
    F->addMatcher(cxxMemberCallExpr(callee(cxxMethodDecl().bind("logger")), hasAncestor(functionDecl().bind("handler")))
                      .bind("call"),
                  this);
  }
  void check(const MatchFinder::MatchResult &R) override {
    const auto *Logger = R.Nodes.getNodeAs<CXXMethodDecl>("logger");
    const auto *Handler = R.Nodes.getNodeAs<FunctionDecl>("handler");
    const auto *Call = R.Nodes.getNodeAs<CXXMemberCallExpr>("call");
    if (!Logger->getDeclName().isIdentifier() || !isCyclicHandler(Handler))
      return;
    static const std::regex EventName("log_(DIAGNOSTIC|ACTIVITY_(LO|HI)|WARNING_(LO|HI)|COMMAND|FATAL)_.*");
    std::string Name = Logger->getNameAsString();
    if (!std::regex_match(Name, EventName) || StringRef(Name).endswith("_ThrottleClear"))
      return;
    if (isThrottled(Logger) || hasThrottleField(Logger) || gatedOnState(Call, Handler, *R.Context))
      return;
    diag(Call->getExprLoc(), "Event %0 is emitted from cyclic handler %1 without throttling or a change-of-state gate.")
        << Name << Handler->getName();
  }

private:
  static bool isCyclicHandler(const FunctionDecl *F) {
    if (!F->getDeclName().isIdentifier())
      return false;
    if (isSchedInHandler(F->getName()))
      return true;
    std::string Lower = F->getName().lower();
    static const std::regex Pattern(".*sched.*_handler|run.*_handler|.*cycle.*_handler|.*tick.*_handler");
    return std::regex_match(Lower, Pattern);
  }
  static bool isThrottled(const CXXMethodDecl *Logger) {
    const FunctionDecl *Def = nullptr;
    if (!Logger->hasBody(Def) || !Def)
      return false;
    llvm::SmallVector<const MemberExpr *, 8> Accesses;
    collect<MemberExpr>(Def->getBody(), Accesses);
    return llvm::any_of(Accesses, [](const MemberExpr *ME) { return ME->getMemberDecl()->getName().endswith("Throttle"); });
  }
  // The FPP `throttle` keyword autocodes an `m_<Event>Throttle` counter into the component base.
  static bool hasThrottleField(const CXXMethodDecl *Logger) {
    static const std::regex Prefix("^log_(DIAGNOSTIC|ACTIVITY_(LO|HI)|WARNING_(LO|HI)|COMMAND|FATAL)_");
    std::string Event = std::regex_replace(Logger->getNameAsString(), Prefix, "");
    const CXXRecordDecl *RD = Logger->getParent();
    while (RD && RD->hasDefinition()) {
      for (const FieldDecl *FD : RD->fields())
        if (FD->getName().endswith("Throttle") && FD->getName().contains(Event))
          return true;
      RD = RD->getNumBases() == 1 ? RD->bases_begin()->getType()->getAsCXXRecordDecl() : nullptr;
    }
    return false;
  }
  static bool gatedOnState(const Stmt *Call, const FunctionDecl *Handler, ASTContext &Ctx) {
    DynTypedNode Cur = DynTypedNode::create(*Call);
    while (true) {
      const auto &Parents = Ctx.getParents(Cur);
      if (Parents.empty())
        return false;
      const Stmt *P = Parents[0].get<Stmt>();
      if (!P)
        return false;
      const Expr *Cond = nullptr;
      if (const auto *If = dyn_cast<IfStmt>(P))
        Cond = containsStmt(If->getCond(), Call) ? nullptr : If->getCond();
      else if (const auto *Sw = dyn_cast<SwitchStmt>(P))
        Cond = containsStmt(Sw->getCond(), Call) ? nullptr : Sw->getCond();
      if (Cond) {
        llvm::SmallVector<const MemberExpr *, 4> Fields;
        collect<MemberExpr>(Cond, Fields);
        if (llvm::any_of(Fields, [](const MemberExpr *ME) { return isa<FieldDecl>(ME->getMemberDecl()); }))
          return true;
      }
      Cur = Parents[0];
    }
  }
};

// cpp/fprime/assert-on-untrusted-input
class AssertOnUntrustedInputCheck : public ClangTidyCheck {
public:
  using ClangTidyCheck::ClangTidyCheck;
  void registerMatchers(MatchFinder *F) override {
    F->addMatcher(functionDecl(isDefinition(), hasBody(stmt())).bind("fn"), this);
  }
  void check(const MatchFinder::MatchResult &R) override {
    const auto *Fn = R.Nodes.getNodeAs<FunctionDecl>("fn");
    const SourceManager &SM = *R.SourceManager;
    if (inTestPath(SM.getFilename(SM.getExpansionLoc(Fn->getLocation()))))
      return;
    // Sources: command arguments and parameter-get results, propagated through local variables.
    std::map<const ValueDecl *, std::string> Tainted;
    if (Fn->getDeclName().isIdentifier() && Fn->getName().endswith("_cmdHandler"))
      for (unsigned I = 2; I < Fn->getNumParams(); ++I)
        Tainted[Fn->getParamDecl(I)] = "command argument " + Fn->getParamDecl(I)->getNameAsString();
    bool Changed = true;
    while (Changed) {
      Changed = false;
      llvm::SmallVector<const VarDecl *, 8> Vars;
      collectVars(Fn->getBody(), Vars);
      for (const VarDecl *V : Vars)
        if (!Tainted.count(V) && V->getInit())
          if (auto What = valueSource(V->getInit(), Tainted)) {
            Tainted[V] = *What;
            Changed = true;
          }
      llvm::SmallVector<const BinaryOperator *, 8> Assigns;
      collect<BinaryOperator>(Fn->getBody(), Assigns);
      for (const BinaryOperator *B : Assigns) {
        if (B->getOpcode() != BO_Assign)
          continue;
        const auto *V = dyn_cast_or_null<VarDecl>(accessedDecl(B->getLHS()->IgnoreParenImpCasts()));
        if (!V || Tainted.count(V) || !V->isLocalVarDeclOrParm())
          continue;
        if (auto What = valueSource(B->getRHS(), Tainted)) {
          Tainted[V] = *What;
          Changed = true;
        }
      }
    }
    llvm::SmallVector<const ConditionalOperator *, 8> Conds;
    collect<ConditionalOperator>(Fn->getBody(), Conds);
    std::set<unsigned> Reported;
    for (const ConditionalOperator *C : Conds) {
      SourceLocation Loc = C->getBeginLoc();
      if (!Loc.isMacroID() || !isAssertMacro(Lexer::getImmediateMacroNameForDiagnostics(Loc, SM, R.Context->getLangOpts())))
        continue;
      const Expr *Asserted = assertedCondition(C);
      if (!Asserted)
        continue;
      auto What = sourceOf(Asserted, Tainted);
      if (!What)
        continue;
      SourceLocation Expansion = SM.getExpansionLoc(Loc);
      if (!Reported.insert(Expansion.getRawEncoding()).second)
        continue;
      diag(Expansion, "FW_ASSERT checks %0, which originates from the ground; validate and report instead.") << *What;
    }
  }

private:
  static void collectVars(const Stmt *S, llvm::SmallVectorImpl<const VarDecl *> &Out) {
    if (!S)
      return;
    if (const auto *DS = dyn_cast<DeclStmt>(S))
      for (const Decl *D : DS->decls())
        if (const auto *V = dyn_cast<VarDecl>(D))
          Out.push_back(V);
    for (const Stmt *C : S->children())
      collectVars(C, Out);
  }
  // The expression itself (through parentheses and casts) is a tainted access or a paramGet_* call.
  static llvm::Optional<std::string> valueSource(const Expr *E, const std::map<const ValueDecl *, std::string> &Tainted) {
    E = E->IgnoreParenImpCasts();
    if (const auto *Cast = dyn_cast<ExplicitCastExpr>(E))
      return valueSource(Cast->getSubExpr(), Tainted);
    if (const ValueDecl *V = accessedDecl(E))
      if (auto It = Tainted.find(V); It != Tainted.end())
        return It->second;
    if (const auto *C = dyn_cast<CallExpr>(E))
      if (const FunctionDecl *F = C->getDirectCallee(); F && F->getDeclName().isIdentifier() && F->getName().startswith("paramGet_"))
        return std::string("parameter value from ") + F->getNameAsString();
    return llvm::None;
  }
  static llvm::Optional<std::string> sourceOf(const Expr *E, const std::map<const ValueDecl *, std::string> &Tainted) {
    llvm::SmallVector<const Expr *, 16> Exprs;
    collect<Expr>(E, Exprs);
    for (const Expr *X : Exprs) {
      if (const ValueDecl *V = accessedDecl(X))
        if (auto It = Tainted.find(V); It != Tainted.end())
          return It->second;
      if (const auto *C = dyn_cast<CallExpr>(X))
        if (const FunctionDecl *F = C->getDirectCallee(); F && F->getDeclName().isIdentifier() && F->getName().startswith("paramGet_"))
          return std::string("parameter value from ") + F->getNameAsString();
    }
    return llvm::None;
  }
};

// cpp/fprime/command-response
class CommandResponseCheck : public ClangTidyCheck {
  struct ClassModel;

public:
  using ClangTidyCheck::ClangTidyCheck;
  void registerMatchers(MatchFinder *F) override {
    F->addMatcher(functionDecl(isDefinition(), hasBody(stmt())).bind("fn"), this);
  }
  // Collect every function definition; handlers are judged once the whole TU is known, since
  // response forwarders and deferred-response readers may live in other classes.
  void check(const MatchFinder::MatchResult &R) override {
    Ctx = R.Context;
    Defs.push_back(R.Nodes.getNodeAs<FunctionDecl>("fn"));
  }
  void onEndOfTranslationUnit() override {
    if (!Ctx)
      return;
    ClassModel Model = buildModel(Defs);
    for (const FunctionDecl *F : Defs)
      if (const auto *H = dyn_cast<CXXMethodDecl>(F); H && isCmdHandler(H))
        judge(H, Model, *Ctx);
    Defs.clear();
    Ctx = nullptr;
  }

private:
  llvm::SmallVector<const FunctionDecl *, 64> Defs;
  ASTContext *Ctx = nullptr;

  void judge(const CXXMethodDecl *H, const ClassModel &Model, ASTContext &Context) {
    const ParmVarDecl *OpParam = H->getParamDecl(0), *SeqParam = H->getParamDecl(1);
    // Fields that hold the deferred (opCode, cmdSeq) pair for this handler.
    std::set<const FieldDecl *> DeferredOp, DeferredSeq;
    for (auto [FOp, FSeq] : Model.DeferredReads) {
      bool StoresOp = false, StoresSeq = false;
      llvm::SmallVector<const Stmt *, 8> Nodes;
      collect<Stmt>(H->getBody(), Nodes);
      for (const Stmt *N : Nodes) {
        if (storesParamToField(N, OpParam, FOp, Model))
          StoresOp = true;
        if (storesParamToField(N, SeqParam, FSeq, Model))
          StoresSeq = true;
      }
      if (StoresOp && StoresSeq) {
        DeferredOp.insert(FOp);
        DeferredSeq.insert(FSeq);
      }
    }
    auto IsAction = [&](const Stmt *S, bool Op) {
      if (const auto *C = dyn_cast<CallExpr>(S))
        if (isDirectResponse(C, OpParam, SeqParam, Model))
          return true;
      const auto &Fields = Op ? DeferredOp : DeferredSeq;
      for (const FieldDecl *F : Fields)
        if (storesParamToField(S, Op ? OpParam : SeqParam, F, Model))
          return true;
      return false;
    };
    if (pathWithoutAction(H, Context, [&](const Stmt *S) { return IsAction(S, true); }) ||
        pathWithoutAction(H, Context, [&](const Stmt *S) { return IsAction(S, false); }))
      diag(H->getLocation(), "Command handler %0 has a path that neither calls cmdResponse_out nor saves opCode and cmdSeq into "
                             "member variables read by a deferred cmdResponse_out call.")
          << H->getName();
  }

  struct ClassModel {
    // Functions forwarding (opCode, cmdSeq) parameters to cmdResponse_out: param indices.
    std::map<const FunctionDecl *, std::set<std::pair<unsigned, unsigned>>> Forwarders;
    // Functions storing a parameter into a field: (param index, field).
    std::map<const FunctionDecl *, std::set<std::pair<unsigned, const FieldDecl *>>> Storers;
    std::set<std::pair<const FieldDecl *, const FieldDecl *>> DeferredReads;
  };

  static bool isCmdResponseOut(const CallExpr *C) {
    const FunctionDecl *F = C->getDirectCallee();
    return F && F->getDeclName().isIdentifier() && F->getName() == "cmdResponse_out" && C->getNumArgs() >= 2;
  }

  // (opExpr, seqExpr) pairs for which this call is a response call.
  static llvm::SmallVector<std::pair<const Expr *, const Expr *>, 2> responseArgs(const CallExpr *C, const ClassModel &M) {
    llvm::SmallVector<std::pair<const Expr *, const Expr *>, 2> Out;
    if (isCmdResponseOut(C))
      Out.push_back({C->getArg(0), C->getArg(1)});
    if (const FunctionDecl *F = C->getDirectCallee())
      if (auto It = M.Forwarders.find(F->getCanonicalDecl()); It != M.Forwarders.end())
        for (auto [OpIdx, SeqIdx] : It->second)
          if (OpIdx < C->getNumArgs() && SeqIdx < C->getNumArgs())
            Out.push_back({C->getArg(OpIdx), C->getArg(SeqIdx)});
    return Out;
  }

  static bool isDirectResponse(const CallExpr *C, const ParmVarDecl *Op, const ParmVarDecl *Seq, const ClassModel &M) {
    if (isCmdResponseOut(C))
      return true;
    for (auto [OpE, SeqE] : responseArgs(C, M))
      if (paramOf(OpE) == Op && paramOf(SeqE) == Seq)
        return true;
    return false;
  }

  // The field that statement S stores parameter P into (directly or through a storer call), else null.
  static const FieldDecl *storedField(const Stmt *S, const ParmVarDecl *P, const ClassModel &M) {
    if (const auto *B = dyn_cast<BinaryOperator>(S); B && B->getOpcode() == BO_Assign)
      return paramOf(B->getRHS()) == P ? fieldOf(B->getLHS()) : nullptr;
    if (const auto *C = dyn_cast<CallExpr>(S))
      if (const FunctionDecl *Callee = C->getDirectCallee())
        if (auto It = M.Storers.find(Callee->getCanonicalDecl()); It != M.Storers.end())
          for (auto [Idx, Field] : It->second)
            if (Idx < C->getNumArgs() && paramOf(C->getArg(Idx)) == P)
              return Field;
    return nullptr;
  }
  static bool storesParamToField(const Stmt *S, const ParmVarDecl *P, const FieldDecl *F, const ClassModel &M) {
    if (const auto *B = dyn_cast<BinaryOperator>(S); B && B->getOpcode() == BO_Assign)
      return fieldOf(B->getLHS()) == F && paramOf(B->getRHS()) == P;
    if (const auto *C = dyn_cast<CallExpr>(S))
      if (const FunctionDecl *Callee = C->getDirectCallee())
        if (auto It = M.Storers.find(Callee->getCanonicalDecl()); It != M.Storers.end())
          for (auto [Idx, Field] : It->second)
            if (Field == F && Idx < C->getNumArgs() && paramOf(C->getArg(Idx)) == P)
              return true;
    return false;
  }

  static ClassModel buildModel(llvm::ArrayRef<const FunctionDecl *> Defs) {
    ClassModel M;
    bool Changed = true;
    while (Changed) {
      Changed = false;
      for (const FunctionDecl *G : Defs) {
        llvm::SmallVector<const CallExpr *, 16> Calls;
        collect<CallExpr>(G->getBody(), Calls);
        for (const CallExpr *C : Calls)
          for (auto [OpE, SeqE] : responseArgs(C, M)) {
            const ParmVarDecl *POp = paramOf(OpE), *PSeq = paramOf(SeqE);
            if (POp && PSeq && POp->getDeclContext() == G && PSeq->getDeclContext() == G)
              Changed |= M.Forwarders[G->getCanonicalDecl()].insert({POp->getFunctionScopeIndex(), PSeq->getFunctionScopeIndex()}).second;
          }
        llvm::SmallVector<const Stmt *, 32> Nodes;
        collect<Stmt>(G->getBody(), Nodes);
        for (const Stmt *N : Nodes)
          for (const ParmVarDecl *P : G->parameters())
            if (const FieldDecl *F = storedField(N, P, M))
              Changed |= M.Storers[G->getCanonicalDecl()].insert({P->getFunctionScopeIndex(), F}).second;
      }
    }
    for (const FunctionDecl *G : Defs) {
      llvm::SmallVector<const CallExpr *, 16> Calls;
      collect<CallExpr>(G->getBody(), Calls);
      for (const CallExpr *C : Calls)
        for (auto [OpE, SeqE] : responseArgs(C, M))
          if (const FieldDecl *FOp = fieldOf(OpE))
            if (const FieldDecl *FSeq = fieldOf(SeqE))
              M.DeferredReads.insert({FOp, FSeq});
    }
    return M;
  }

  // Is there a path from entry to exit along which no element satisfies IsAction?
  template <typename Pred> static bool pathWithoutAction(const FunctionDecl *H, ASTContext &Ctx, Pred IsAction) {
    CFG::BuildOptions Options;
    Options.setAllAlwaysAdd();
    std::unique_ptr<CFG> G = CFG::buildCFG(H, H->getBody(), &Ctx, Options);
    if (!G)
      return false;
    std::set<const CFGBlock *> Visited;
    llvm::SmallVector<const CFGBlock *, 16> Work{&G->getEntry()};
    while (!Work.empty()) {
      const CFGBlock *B = Work.pop_back_val();
      if (!Visited.insert(B).second)
        continue;
      if (B == &G->getExit())
        return true;
      bool Acted = false;
      for (const CFGElement &E : *B)
        if (auto CS = E.getAs<CFGStmt>(); CS && IsAction(CS->getStmt()))
          Acted = true;
      if (Acted)
        continue;
      for (const CFGBlock *Succ : B->succs())
        if (Succ)
          Work.push_back(Succ);
    }
    return false;
  }
};

void registerModelChecks(ClangTidyCheckFactories &F) {
  F.registerCheck<UninitializedMemberCheck>("fprime-uninitialized-member");
  F.registerCheck<UnthrottledCyclicEventCheck>("fprime-unthrottled-cyclic-event");
  F.registerCheck<AssertOnUntrustedInputCheck>("fprime-assert-on-untrusted-input");
  F.registerCheck<CommandResponseCheck>("fprime-command-response");
}

} // namespace fprime
} // namespace tidy
} // namespace clang
