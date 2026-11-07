// Structural (AST-only) checks ported from the fprime CodeQL queries.
#include "FprimeChecks.h"

#include "clang/AST/ParentMapContext.h"
#include "clang/Lex/PPCallbacks.h"
#include "clang/Lex/Preprocessor.h"

#include <regex>

using namespace clang::ast_matchers;

namespace clang {
namespace tidy {
namespace fprime {

// cpp/fprime/no-printf
class NoPrintfCheck : public ClangTidyCheck {
public:
  using ClangTidyCheck::ClangTidyCheck;
  void registerMatchers(MatchFinder *F) override {
    F->addMatcher(callExpr(callee(functionDecl(unless(cxxMethodDecl()),
                                               hasAnyName("printf", "fprintf", "sprintf", "snprintf", "vprintf",
                                                          "vfprintf", "vsprintf", "vsnprintf", "puts", "putchar"))))
                      .bind("call"),
                  this);
  }
  void check(const MatchFinder::MatchResult &R) override {
    const auto *Call = R.Nodes.getNodeAs<CallExpr>("call");
    diag(Call->getBeginLoc(), "Call to %0; use F Prime events/logging instead.")
        << Call->getDirectCallee()->getName();
  }
};

// cpp/fprime/switch-default
class SwitchDefaultCheck : public ClangTidyCheck {
public:
  using ClangTidyCheck::ClangTidyCheck;
  void registerMatchers(MatchFinder *F) override {
    F->addMatcher(switchStmt(unless(hasDescendant(defaultStmt()))).bind("sw"), this);
  }
  void check(const MatchFinder::MatchResult &R) override {
    const auto *S = R.Nodes.getNodeAs<SwitchStmt>("sw");
    // hasDescendant also finds defaults of nested switches; verify against the case list.
    for (const SwitchCase *C = S->getSwitchCaseList(); C; C = C->getNextSwitchCase())
      if (isa<DefaultStmt>(C))
        return;
    diag(S->getBeginLoc(), "This switch statement does not have a default case.");
  }
};

// cpp/fprime/switch-case-fallthrough
class SwitchCaseFallthroughCheck : public ClangTidyCheck {
public:
  using ClangTidyCheck::ClangTidyCheck;
  void registerMatchers(MatchFinder *F) override { F->addMatcher(switchStmt().bind("sw"), this); }
  void check(const MatchFinder::MatchResult &R) override {
    const auto *S = R.Nodes.getNodeAs<SwitchStmt>("sw");
    const auto *Body = dyn_cast_or_null<CompoundStmt>(S->getBody());
    if (!Body)
      return;
    // Walk the body's statements; a case label owns the statements that follow it
    // until the next case label (CodeQL's SwitchCase.getAStmt()).
    llvm::SmallVector<const Stmt *, 16> Stmts(Body->body_begin(), Body->body_end());
    for (size_t I = 0; I < Stmts.size(); ++I) {
      const auto *Case = dyn_cast<SwitchCase>(Stmts[I]);
      if (!Case)
        continue;
      const Stmt *Last = nullptr;
      // The label's own sub-statement (case X: stmt;) counts as its first statement.
      const Stmt *Sub = Case->getSubStmt();
      while (Sub && isa<SwitchCase>(Sub))
        Sub = cast<SwitchCase>(Sub)->getSubStmt();
      if (Sub && !isa<NullStmt>(Sub))
        Last = Sub;
      size_t J = I + 1;
      for (; J < Stmts.size() && !isa<SwitchCase>(Stmts[J]); ++J)
        Last = Stmts[J];
      bool IsLast = J >= Stmts.size();
      if (!Last || IsLast || transfersControl(Last))
        continue;
      if (hasFallthroughComment(Last, Stmts[J], *R.SourceManager, R.Context->getLangOpts()))
        continue;
      diag(Case->getBeginLoc(),
           "This non-empty switch case falls through to the next case without a fall-through annotation.");
    }
  }

private:
  static bool transfersControl(const Stmt *S) {
    if (!S)
      return false;
    if (isa<BreakStmt>(S) || isa<ReturnStmt>(S) || isa<ContinueStmt>(S) || isa<GotoStmt>(S) ||
        isa<CXXThrowExpr>(S))
      return true;
    if (const auto *Block = dyn_cast<CompoundStmt>(S))
      return !Block->body_empty() && transfersControl(Block->body_back());
    if (const auto *A = dyn_cast<AttributedStmt>(S))
      return llvm::any_of(A->getAttrs(), [](const Attr *At) { return isa<FallThroughAttr>(At); });
    return false;
  }
  static bool hasFallthroughComment(const Stmt *Last, const Stmt *Next, const SourceManager &SM,
                                    const LangOptions &LO) {
    SourceLocation Begin = SM.getExpansionLoc(Last->getEndLoc());
    SourceLocation End = SM.getExpansionLoc(Next->getBeginLoc());
    if (Begin.isInvalid() || End.isInvalid() || !SM.isWrittenInSameFile(Begin, End))
      return false;
    // Include the full lines spanned by the two statements, as CodeQL does.
    FileID FID = SM.getFileID(Begin);
    unsigned StartLine = SM.getSpellingLineNumber(Begin);
    unsigned EndLine = SM.getSpellingLineNumber(End);
    SourceLocation From = SM.translateLineCol(FID, StartLine, 1);
    SourceLocation To = SM.translateLineCol(FID, EndLine + 1, 1);
    if (From.isInvalid() || To.isInvalid())
      return false;
    StringRef Text = Lexer::getSourceText(CharSourceRange::getCharRange(From, To), SM, LO);
    static const std::regex Marker("fall[ -]?thr(ough|u)", std::regex::icase);
    return std::regex_search(Text.begin(), Text.end(), Marker);
  }
};

// cpp/fprime/compound-statements
class CompoundStatementsCheck : public ClangTidyCheck {
public:
  using ClangTidyCheck::ClangTidyCheck;
  void registerMatchers(MatchFinder *F) override {
    F->addMatcher(forStmt(hasBody(stmt(unless(compoundStmt())).bind("body"))).bind("for"), this);
    F->addMatcher(whileStmt(hasBody(stmt(unless(compoundStmt())).bind("body"))).bind("while"), this);
    F->addMatcher(doStmt(hasBody(stmt(unless(compoundStmt())).bind("body"))).bind("do"), this);
    F->addMatcher(ifStmt(hasThen(stmt(unless(compoundStmt())).bind("body"))).bind("if"), this);
    F->addMatcher(ifStmt(hasElse(stmt(unless(anyOf(compoundStmt(), ifStmt()))).bind("body"))).bind("else"), this);
  }
  void check(const MatchFinder::MatchResult &R) override {
    const auto *Body = R.Nodes.getNodeAs<Stmt>("body");
    StringRef Kind = R.Nodes.getNodeAs<Stmt>("for")     ? "for loop"
                     : R.Nodes.getNodeAs<Stmt>("while") ? "while loop"
                     : R.Nodes.getNodeAs<Stmt>("do")    ? "do-while loop"
                     : R.Nodes.getNodeAs<Stmt>("if")    ? "if statement"
                                                        : "else clause";
    diag(Body->getBeginLoc(), "The body of this %0 is not a compound statement (missing braces).") << Kind;
  }
};

// cpp/fprime/no-function-decl-in-function
class NoFunctionDeclInFunctionCheck : public ClangTidyCheck {
public:
  using ClangTidyCheck::ClangTidyCheck;
  void registerMatchers(MatchFinder *F) override {
    F->addMatcher(declStmt(hasSingleDecl(functionDecl().bind("fn")), hasAncestor(functionDecl().bind("outer")))
                      .bind("ds"),
                  this);
  }
  void check(const MatchFinder::MatchResult &R) override {
    const auto *DS = R.Nodes.getNodeAs<DeclStmt>("ds");
    diag(DS->getBeginLoc(), "Function %0 is declared inside the body of function %1.")
        << R.Nodes.getNodeAs<FunctionDecl>("fn")->getName() << R.Nodes.getNodeAs<FunctionDecl>("outer")->getName();
  }
};

// cpp/fprime/jpl-c/multiple-var-decls-per-line
class MultipleVarDeclsPerLineCheck : public ClangTidyCheck {
public:
  using ClangTidyCheck::ClangTidyCheck;
  void registerMatchers(MatchFinder *F) override { F->addMatcher(declStmt(unless(hasSingleDecl(decl()))).bind("ds"), this); }
  void check(const MatchFinder::MatchResult &R) override {
    const auto *DS = R.Nodes.getNodeAs<DeclStmt>("ds");
    const SourceManager &SM = *R.SourceManager;
    llvm::SmallVector<unsigned, 4> Lines;
    for (const Decl *D : DS->decls()) {
      if (!isa<VarDecl>(D) || D->isImplicit())
        continue;
      unsigned Line = SM.getExpansionLineNumber(D->getLocation());
      if (llvm::is_contained(Lines, Line)) {
        diag(DS->getBeginLoc(), "Multiple variable declarations on the same line.");
        return;
      }
      Lines.push_back(Line);
    }
  }
};

// cpp/fprime/public-data-member
class PublicDataMemberCheck : public ClangTidyCheck {
public:
  using ClangTidyCheck::ClangTidyCheck;
  void registerMatchers(MatchFinder *F) override {
    F->addMatcher(fieldDecl(isPublic(), unless(hasType(isConstQualified())),
                            hasParent(cxxRecordDecl(unless(isStruct()), unless(isUnion()),
                                                    has(cxxMethodDecl(unless(isImplicit()), unless(cxxConstructorDecl()),
                                                                      unless(cxxDestructorDecl()))))
                                          .bind("class")))
                      .bind("field"),
                  this);
  }
  void check(const MatchFinder::MatchResult &R) override {
    const auto *FD = R.Nodes.getNodeAs<FieldDecl>("field");
    diag(FD->getLocation(), "Data member %0 of class %1 is public; consider making it private or protected.")
        << FD->getName() << R.Nodes.getNodeAs<CXXRecordDecl>("class")->getName();
  }
};

// cpp/fprime/float-equality
class FloatEqualityCheck : public ClangTidyCheck {
public:
  using ClangTidyCheck::ClangTidyCheck;
  void registerMatchers(MatchFinder *F) override {
    F->addMatcher(binaryOperator(hasAnyOperatorName("==", "!="),
                                 hasEitherOperand(ignoringParenImpCasts(hasType(realFloatingPointType()))))
                      .bind("eq"),
                  this);
  }
  void check(const MatchFinder::MatchResult &R) override {
    const auto *Eq = R.Nodes.getNodeAs<BinaryOperator>("eq");
    const auto *L = dyn_cast<DeclRefExpr>(Eq->getLHS()->IgnoreParenImpCasts());
    const auto *Rr = dyn_cast<DeclRefExpr>(Eq->getRHS()->IgnoreParenImpCasts());
    if (L && Rr && L->getDecl() == Rr->getDecl())
      return; // NaN check x != x
    diag(Eq->getOperatorLoc(), "Floating-point values are compared with %0; compare against a tolerance instead.")
        << Eq->getOpcodeStr();
  }
};

// cpp/fprime/float-loop-control
class FloatLoopControlCheck : public ClangTidyCheck {
public:
  using ClangTidyCheck::ClangTidyCheck;
  void registerMatchers(MatchFinder *F) override {
    auto FloatVar = declRefExpr(to(varDecl(hasType(realFloatingPointType())))).bind("var");
    F->addMatcher(forStmt(hasCondition(forEachDescendant(FloatVar))).bind("loop"), this);
    F->addMatcher(forStmt(hasCondition(FloatVar)).bind("loop"), this);
    F->addMatcher(whileStmt(hasCondition(anyOf(FloatVar, forEachDescendant(FloatVar)))).bind("loop"), this);
    F->addMatcher(doStmt(hasCondition(anyOf(FloatVar, forEachDescendant(FloatVar)))).bind("loop"), this);
  }
  void check(const MatchFinder::MatchResult &R) override {
    const auto *Loop = R.Nodes.getNodeAs<Stmt>("loop");
    const auto *Var = R.Nodes.getNodeAs<DeclRefExpr>("var");
    auto Key = std::make_pair(Loop, Var->getDecl());
    if (!Seen.insert(Key).second)
      return;
    diag(Loop->getBeginLoc(), "This loop's condition depends on the floating-point variable %0.")
        << Var->getDecl()->getName();
  }

private:
  std::set<std::pair<const Stmt *, const Decl *>> Seen;
};

// cpp/fprime/global-mutable-variable
class GlobalMutableVariableCheck : public ClangTidyCheck {
public:
  using ClangTidyCheck::ClangTidyCheck;
  void registerMatchers(MatchFinder *F) override {
    F->addMatcher(varDecl(unless(parmVarDecl()), hasGlobalStorage(), unless(isConstexpr()), isDefinition(),
                          unless(hasType(isConstQualified())))
                      .bind("var"),
                  this);
  }
  void check(const MatchFinder::MatchResult &R) override {
    const auto *V = R.Nodes.getNodeAs<VarDecl>("var");
    if (V->isStaticDataMember() || V->isImplicit())
      return;
    if (inTestPath(R.SourceManager->getFilename(R.SourceManager->getExpansionLoc(V->getLocation()))))
      return;
    if (isDeeplyConst(V->getType()))
      return;
    StringRef Kind = V->isStaticLocal() ? "Function-static" : "Namespace-scope";
    diag(V->getLocation(), "%0 variable %1 is mutable; declare it local or as a member.") << Kind << V->getName();
  }

private:
  static bool isDeeplyConst(QualType T) {
    T = T.getNonReferenceType();
    if (const auto *Arr = T->getAsArrayTypeUnsafe())
      return isDeeplyConst(Arr->getElementType());
    return T.isConstQualified();
  }
};

// cpp/fprime/no-float-in-isr
class NoFloatInIsrCheck : public ClangTidyCheck {
public:
  using ClangTidyCheck::ClangTidyCheck;
  void registerMatchers(MatchFinder *F) override {
    auto Float = ignoringParenImpCasts(hasType(realFloatingPointType()));
    F->addMatcher(binaryOperator(anyOf(hasType(realFloatingPointType()), hasEitherOperand(Float)),
                                 hasAncestor(functionDecl().bind("fn")))
                      .bind("op"),
                  this);
    F->addMatcher(unaryOperator(anyOf(hasType(realFloatingPointType()), hasUnaryOperand(Float)),
                                hasAncestor(functionDecl().bind("fn")))
                      .bind("op"),
                  this);
  }
  void check(const MatchFinder::MatchResult &R) override {
    const auto *Fn = R.Nodes.getNodeAs<FunctionDecl>("fn");
    if (!isIsrFunction(Fn))
      return;
    const auto *Op = R.Nodes.getNodeAs<Expr>("op");
    diag(Op->getExprLoc(), "Floating-point operation in interrupt service routine %0.") << Fn->getName();
  }
};

// cpp/fprime/isr-complexity
class IsrComplexityCheck : public ClangTidyCheck {
public:
  using ClangTidyCheck::ClangTidyCheck;
  void registerMatchers(MatchFinder *F) override { F->addMatcher(functionDecl(isDefinition(), hasBody(stmt())).bind("fn"), this); }
  void check(const MatchFinder::MatchResult &R) override {
    const auto *Fn = R.Nodes.getNodeAs<FunctionDecl>("fn");
    if (!isIsrFunction(Fn))
      return;
    bool HasLoop = false;
    unsigned Count = 0;
    countStmts(Fn->getBody(), HasLoop, Count);
    if (HasLoop)
      diag(Fn->getLocation(), "Interrupt service routine %0 contains a loop; keep ISRs minimal.") << Fn->getName();
    else if (Count > 20)
      diag(Fn->getLocation(), "Interrupt service routine %0 has %1 statements; keep ISRs minimal.")
          << Fn->getName() << Count;
  }

private:
  // CodeQL counts Stmt nodes (ExprStmt included) other than blocks; clang has no ExprStmt,
  // so count every statement-position child instead.
  static void countStmts(const Stmt *S, bool &HasLoop, unsigned &Count) {
    if (!S)
      return;
    for (const Stmt *C : S->children()) {
      if (!C)
        continue;
      bool StmtPosition = isa<CompoundStmt>(S) || !isa<Expr>(C);
      if (!StmtPosition)
        continue;
      if (isa<ForStmt>(C) || isa<WhileStmt>(C) || isa<DoStmt>(C) || isa<CXXForRangeStmt>(C))
        HasLoop = true;
      if (!isa<CompoundStmt>(C))
        ++Count;
      if (!isa<Expr>(C))
        countStmts(C, HasLoop, Count);
    }
  }
};

// cpp/fprime/incomplete-code -- comment markers via the preprocessor's comment handler.
class IncompleteCodeCheck : public ClangTidyCheck, public CommentHandler {
public:
  using ClangTidyCheck::ClangTidyCheck;
  void registerPPCallbacks(const SourceManager &, Preprocessor *PP, Preprocessor *) override {
    PP->addCommentHandler(this);
  }
  bool HandleComment(Preprocessor &PP, SourceRange Range) override {
    const SourceManager &SM = PP.getSourceManager();
    if (!fromSource(Range.getBegin(), SM))
      return false;
    StringRef Text = Lexer::getSourceText(CharSourceRange::getCharRange(Range), SM, PP.getLangOpts());
    static const std::regex Marker("\\b(TBD|TODO|FIXME)\\b");
    std::cmatch M;
    if (std::regex_search(Text.begin(), Text.end(), M, Marker))
      diag(Range.getBegin(), "Comment contains the incomplete-code marker %0.") << std::string(M[1]);
    return false;
  }
};

// cpp/fprime/define-constant -- numeric object-like macros.
class DefineConstantCheck : public ClangTidyCheck {
  class Callbacks : public PPCallbacks {
  public:
    Callbacks(DefineConstantCheck &C, const SourceManager &SM, const LangOptions &LO) : Check(C), SM(SM), LO(LO) {}
    void MacroDefined(const Token &Name, const MacroDirective *MD) override {
      const MacroInfo *MI = MD->getMacroInfo();
      if (!MI || MI->isFunctionLike() || MI->isBuiltinMacro() || !fromSource(MI->getDefinitionLoc(), SM))
        return;
      StringRef Path = SM.getFilename(MI->getDefinitionLoc());
      if (Path.empty() || Path.startswith("<")) // -D on the command line, <built-in>
        return;
      if (Path.contains("/config/") || inTestPath(Path) || Path.endswith("Ac.hpp") || Path.endswith("Ac.cpp"))
        return;
      std::string Body;
      for (const Token &T : MI->tokens()) {
        Body += Lexer::getSpelling(T, SM, LO);
        Body += ' ';
      }
      static const std::regex Numeric(
          "^\\(?\\s*[-+]?\\s*(0[xX][0-9a-fA-F]+|[0-9]+(\\.[0-9]*)?([eE][-+]?[0-9]+)?)[uUlLfF]*\\s*\\)?\\s*$");
      if (std::regex_match(Body, Numeric))
        Check.diag(MI->getDefinitionLoc(), "Numeric constant %0 is defined with #define; use a typed constant.")
            << Name.getIdentifierInfo()->getName();
    }

  private:
    DefineConstantCheck &Check;
    const SourceManager &SM;
    const LangOptions &LO;
  };

public:
  using ClangTidyCheck::ClangTidyCheck;
  void registerPPCallbacks(const SourceManager &SM, Preprocessor *PP, Preprocessor *) override {
    PP->addPPCallbacks(std::make_unique<Callbacks>(*this, SM, PP->getLangOpts()));
  }
};

void registerSyntaxChecks(ClangTidyCheckFactories &F) {
  F.registerCheck<NoPrintfCheck>("fprime-no-printf");
  F.registerCheck<SwitchDefaultCheck>("fprime-switch-default");
  F.registerCheck<SwitchCaseFallthroughCheck>("fprime-switch-case-fallthrough");
  F.registerCheck<CompoundStatementsCheck>("fprime-compound-statements");
  F.registerCheck<NoFunctionDeclInFunctionCheck>("fprime-no-function-decl-in-function");
  F.registerCheck<MultipleVarDeclsPerLineCheck>("fprime-multiple-var-decls-per-line");
  F.registerCheck<PublicDataMemberCheck>("fprime-public-data-member");
  F.registerCheck<FloatEqualityCheck>("fprime-float-equality");
  F.registerCheck<FloatLoopControlCheck>("fprime-float-loop-control");
  F.registerCheck<GlobalMutableVariableCheck>("fprime-global-mutable-variable");
  F.registerCheck<NoFloatInIsrCheck>("fprime-no-float-in-isr");
  F.registerCheck<IsrComplexityCheck>("fprime-isr-complexity");
  F.registerCheck<IncompleteCodeCheck>("fprime-incomplete-code");
  F.registerCheck<DefineConstantCheck>("fprime-define-constant");
}

} // namespace fprime
} // namespace tidy
} // namespace clang
