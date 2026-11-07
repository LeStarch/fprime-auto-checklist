// JPL Rule 16 assertion checks (cpp/fprime/jpl-c/use-of-assertions-*).
#include "FprimeChecks.h"

#include "clang/AST/ParentMapContext.h"
#include "clang/AST/RecursiveASTVisitor.h"

#include <regex>

using namespace clang::ast_matchers;

namespace clang {
namespace tidy {
namespace fprime {

namespace {

// The outermost ConditionalOperator produced by one FW_ASSERT expansion carries the asserted condition.
AST_MATCHER(ConditionalOperator, isOutermostAssertExpansion) {
  const SourceManager &SM = Finder->getASTContext().getSourceManager();
  SourceLocation Loc = Node.getBeginLoc();
  if (!Loc.isMacroID())
    return false;
  StringRef Macro = Lexer::getImmediateMacroNameForDiagnostics(Loc, SM, Finder->getASTContext().getLangOpts());
  if (!isAssertMacro(Macro))
    return false;
  // Outermost: no ancestor ConditionalOperator from the same expansion.
  SourceLocation Expansion = SM.getExpansionLoc(Loc);
  const Stmt *Cur = &Node;
  while (true) {
    const auto &Parents = Finder->getASTContext().getParents(*Cur);
    if (Parents.empty())
      break;
    const Stmt *P = Parents[0].get<Stmt>();
    if (!P)
      break;
    if (const auto *PC = dyn_cast<ConditionalOperator>(P))
      if (PC->getBeginLoc().isMacroID() && SM.getExpansionLoc(PC->getBeginLoc()) == Expansion)
        return false;
    Cur = P;
  }
  return true;
}

auto assertion() { return conditionalOperator(isOutermostAssertExpansion()).bind("assert"); }

bool bodyMayHaveSideEffects(const Stmt *S, unsigned Depth);

bool callMayHaveSideEffects(const CallExpr *Call, unsigned Depth) {
  const FunctionDecl *Callee = Call->getDirectCallee();
  if (!Callee)
    return true; // indirect call
  if (const auto *M = dyn_cast<CXXMethodDecl>(Callee)) {
    if (M->isConst())
      return false;
    // hasConstOverload: a const sibling with the same name/arity.
    for (const CXXMethodDecl *Sibling : M->getParent()->methods())
      if (Sibling->isConst() && Sibling->getDeclName() == M->getDeclName() &&
          Sibling->getNumParams() == M->getNumParams())
        return false;
    if (M->isVirtual())
      return true;
  }
  if (Callee->getQualifiedNameAsString() == "Fw::StringUtils::string_length")
    return false;
  if (Callee->hasAttr<ConstAttr>() || Callee->hasAttr<PureAttr>() || Callee->isConstexpr())
    return false;
  const FunctionDecl *Def = nullptr;
  if (!Callee->hasBody(Def) || !Def || Depth > 3)
    return true;
  return bodyMayHaveSideEffects(Def->getBody(), Depth + 1);
}

// Does the statement write non-local state, allocate, throw, or call something that may?
bool bodyMayHaveSideEffects(const Stmt *S, unsigned Depth) {
  if (!S)
    return false;
  if (const auto *B = dyn_cast<BinaryOperator>(S)) {
    if (B->isAssignmentOp()) {
      const Expr *L = B->getLHS()->IgnoreParenImpCasts();
      const auto *DRE = dyn_cast<DeclRefExpr>(L);
      bool Local = DRE && isa<VarDecl>(DRE->getDecl()) && cast<VarDecl>(DRE->getDecl())->isLocalVarDecl() &&
                   !DRE->getType()->isReferenceType();
      if (!Local)
        return true;
    }
  } else if (const auto *U = dyn_cast<UnaryOperator>(S)) {
    if (U->isIncrementDecrementOp()) {
      const auto *DRE = dyn_cast<DeclRefExpr>(U->getSubExpr()->IgnoreParenImpCasts());
      bool Local = DRE && isa<VarDecl>(DRE->getDecl()) && cast<VarDecl>(DRE->getDecl())->isLocalVarDecl();
      if (!Local)
        return true;
    }
  } else if (isa<CXXNewExpr>(S) || isa<CXXDeleteExpr>(S) || isa<CXXThrowExpr>(S) || isa<AsmStmt>(S)) {
    return true;
  } else if (const auto *C = dyn_cast<CallExpr>(S)) {
    if (callMayHaveSideEffects(C, Depth))
      return true;
  }
  for (const Stmt *Child : S->children())
    if (bodyMayHaveSideEffects(Child, Depth))
      return true;
  return false;
}

// Direct side effects of the asserted expression itself (CodeQL isSideEffecting over getAChild*()).
bool exprIsSideEffecting(const Stmt *S) {
  if (!S)
    return false;
  if (const auto *B = dyn_cast<BinaryOperator>(S); B && B->isAssignmentOp())
    return true;
  if (const auto *U = dyn_cast<UnaryOperator>(S); U && U->isIncrementDecrementOp())
    return true;
  if (isa<CXXNewExpr>(S) || isa<CXXDeleteExpr>(S) || isa<CXXThrowExpr>(S))
    return true;
  if (const auto *C = dyn_cast<CallExpr>(S); C && !isa<CXXOperatorCallExpr>(C) && callMayHaveSideEffects(C, 0))
    return true;
  if (const auto *C = dyn_cast<CXXOperatorCallExpr>(S); C && C->isAssignmentOp())
    return true;
  for (const Stmt *Child : S->children())
    if (exprIsSideEffecting(Child))
      return true;
  return false;
}

} // namespace

bool UseOfAssertionsConstantCheck_readsThroughObject(const Stmt *S);

// cpp/fprime/jpl-c/use-of-assertions-constant
class UseOfAssertionsConstantCheck : public ClangTidyCheck {
public:
  using ClangTidyCheck::ClangTidyCheck;
  void registerMatchers(MatchFinder *F) override { F->addMatcher(assertion(), this); }
  void check(const MatchFinder::MatchResult &R) override {
    const Expr *Asserted = assertedCondition(R.Nodes.getNodeAs<ConditionalOperator>("assert"));
    if (!Asserted || Asserted->isValueDependent())
      return;
    if (readsThroughObject(Asserted))
      return; // CodeQL does not fold `obj.ENUMERATOR`; neither do we
    Expr::EvalResult Result;
    if (!Asserted->EvaluateAsRValue(Result, *R.Context) || !Result.Val.isInt())
      return;
    bool IsZero = Result.Val.getInt() == 0;
    bool IsLiteral = isa<IntegerLiteral>(Asserted) || isa<CXXBoolLiteralExpr>(Asserted);
    if (IsLiteral && IsZero)
      return; // FW_ASSERT(false) / FW_ASSERT(0): unreachable-code idiom
    diag(Asserted->getBeginLoc(), IsZero ? "This assertion is always false." : "This assertion is always true.");
  }

private:
  static bool readsThroughObject(const Stmt *S) { return UseOfAssertionsConstantCheck_readsThroughObject(S); }
};

bool UseOfAssertionsConstantCheck_readsThroughObject(const Stmt *S) {
  if (const auto *ME = dyn_cast<MemberExpr>(S))
    if (!isa<CXXThisExpr>(ME->getBase()->IgnoreParenImpCasts()) &&
        (isa<EnumConstantDecl>(ME->getMemberDecl()) || isa<VarDecl>(ME->getMemberDecl())))
      return true;
  for (const Stmt *C : S->children())
    if (C && UseOfAssertionsConstantCheck_readsThroughObject(C))
      return true;
  return false;
}

// cpp/fprime/jpl-c/use-of-assertions-non-boolean
class UseOfAssertionsNonBooleanCheck : public ClangTidyCheck {
public:
  using ClangTidyCheck::ClangTidyCheck;
  void registerMatchers(MatchFinder *F) override { F->addMatcher(assertion(), this); }
  void check(const MatchFinder::MatchResult &R) override {
    const Expr *Asserted = assertedCondition(R.Nodes.getNodeAs<ConditionalOperator>("assert"));
    if (!Asserted || Asserted->isTypeDependent())
      return;
    if (Asserted->getType().getUnqualifiedType()->isBooleanType())
      return;
    diag(Asserted->getBeginLoc(), "Assertions should be defined as Boolean tests.");
  }
};

// cpp/fprime/jpl-c/use-of-assertions-side-effect
class UseOfAssertionsSideEffectCheck : public ClangTidyCheck {
public:
  using ClangTidyCheck::ClangTidyCheck;
  void registerMatchers(MatchFinder *F) override { F->addMatcher(assertion(), this); }
  void check(const MatchFinder::MatchResult &R) override {
    const Expr *Asserted = assertedCondition(R.Nodes.getNodeAs<ConditionalOperator>("assert"));
    if (!Asserted)
      return;
    if (exprIsSideEffecting(Asserted))
      diag(Asserted->getBeginLoc(), "Assertions should not have side effects.");
  }
};

// cpp/fprime/jpl-c/use-of-assertions-density
class UseOfAssertionsDensityCheck : public ClangTidyCheck {
public:
  using ClangTidyCheck::ClangTidyCheck;
  void registerMatchers(MatchFinder *F) override {
    F->addMatcher(functionDecl(isDefinition(), hasBody(compoundStmt()), unless(isImplicit()),
                               unless(cxxConstructorDecl()), unless(cxxDestructorDecl()))
                      .bind("fn"),
                  this);
  }
  void check(const MatchFinder::MatchResult &R) override {
    const auto *Fn = R.Nodes.getNodeAs<FunctionDecl>("fn");
    if (Fn->isTemplateInstantiation() || Fn->getTemplatedKind() == FunctionDecl::TK_FunctionTemplate)
      return;
    const SourceManager &SM = *R.SourceManager;
    SourceRange Range(SM.getExpansionLoc(Fn->getBeginLoc()), SM.getExpansionLoc(Fn->getEndLoc()));
    if (Range.isInvalid() || !SM.isWrittenInSameFile(Range.getBegin(), Range.getEnd()))
      return;
    StringRef Text = Lexer::getSourceText(CharSourceRange::getTokenRange(Range), SM, R.Context->getLangOpts());
    if (linesOfCode(Text) <= 10)
      return;
    static const std::regex Asserts("\\b(FW_ASSERT|assert|static_assert|(ASSERT|EXPECT)_[A-Z_0-9]+)\\s*\\(");
    if (std::regex_search(Text.begin(), Text.end(), Asserts))
      return;
    QualType Ret = Fn->getReturnType().getUnqualifiedType();
    if (Ret->isBooleanType() || Ret->isEnumeralType() || isFppEnumClass(Ret))
      return;
    if (logsEvents(Fn->getBody()))
      return;
    if (isAssertInfrastructure(Fn))
      return;
    diag(Fn->getLocation(), "All functions of more than 10 lines should have at least one assertion, unless they "
                            "handle errors by returning a status enum or bool, or by emitting a WARNING/FATAL event.");
  }

private:
  static unsigned linesOfCode(StringRef Text) {
    unsigned Count = 0;
    bool InBlockComment = false;
    llvm::SmallVector<StringRef, 32> Lines;
    Text.split(Lines, '\n');
    for (StringRef Line : Lines) {
      std::string Code;
      for (size_t I = 0; I < Line.size(); ++I) {
        if (InBlockComment) {
          if (Line.substr(I).startswith("*/")) {
            InBlockComment = false;
            ++I;
          }
          continue;
        }
        if (Line.substr(I).startswith("//"))
          break;
        if (Line.substr(I).startswith("/*")) {
          InBlockComment = true;
          ++I;
          continue;
        }
        Code += Line[I];
      }
      if (!StringRef(Code).trim().empty())
        ++Count;
    }
    return Count;
  }
  static bool isFppEnumClass(QualType T) {
    const auto *RD = T->getAsCXXRecordDecl();
    if (!RD)
      return false;
    for (const Decl *D : RD->decls())
      if (const auto *E = dyn_cast<EnumDecl>(D); E && E->getName() == "T")
        return true;
    return false;
  }
  static bool logsEvents(const Stmt *S) {
    if (!S)
      return false;
    if (const auto *C = dyn_cast<CallExpr>(S))
      if (const FunctionDecl *Callee = C->getDirectCallee())
        if (Callee->getName().startswith("log_WARNING") || Callee->getName().startswith("log_FATAL"))
          return true;
    for (const Stmt *Child : S->children())
      if (logsEvents(Child))
        return true;
    return false;
  }
  static bool isAssertInfrastructure(const FunctionDecl *Fn) {
    std::string QN = Fn->getQualifiedNameAsString();
    if (QN == "CAssert0" || QN == "CAssert1" || QN == "Fw::formatAssertMessage" || QN == "Fw::printAssertMessage" ||
        QN == "Fw::terminateOnAssert")
      return true;
    if (const auto *M = dyn_cast<CXXMethodDecl>(Fn)) {
      const CXXRecordDecl *RD = M->getParent();
      if (RD->getQualifiedNameAsString() == "Fw::AssertHook")
        return true;
      if (RD->hasDefinition())
        for (const CXXBaseSpecifier &B : RD->bases())
          if (const auto *Base = B.getType()->getAsCXXRecordDecl();
              Base && Base->getQualifiedNameAsString() == "Fw::AssertHook")
            return true;
    }
    return false;
  }
};

void registerAssertionChecks(ClangTidyCheckFactories &F) {
  F.registerCheck<UseOfAssertionsConstantCheck>("fprime-use-of-assertions-constant");
  F.registerCheck<UseOfAssertionsNonBooleanCheck>("fprime-use-of-assertions-non-boolean");
  F.registerCheck<UseOfAssertionsSideEffectCheck>("fprime-use-of-assertions-side-effect");
  F.registerCheck<UseOfAssertionsDensityCheck>("fprime-use-of-assertions-density");
}

} // namespace fprime
} // namespace tidy
} // namespace clang
