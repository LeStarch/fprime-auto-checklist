// Structural (AST-only) checks ported from the fprime CodeQL queries.
#include "FprimeChecks.h"

#include "clang/AST/Attr.h"
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
        << identName(Call->getDirectCallee());
  }
};

// cpp/fprime/switch-default
class SwitchDefaultCheck : public ClangTidyCheck {
public:
  using ClangTidyCheck::ClangTidyCheck;
  void registerMatchers(MatchFinder *F) override {
    F->addMatcher(switchStmt().bind("sw"), this);
  }
  void check(const MatchFinder::MatchResult &R) override {
    const auto *S = R.Nodes.getNodeAs<SwitchStmt>("sw");
    // Only this switch's own case list counts; nested switches have their own.
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
      // `[[fallthrough]];` is the standard annotation; CodeQL only recognises comments
      // (a CodeQL defect we deliberately do not mirror).
      if (const auto *AS = dyn_cast<AttributedStmt>(Last);
          AS && llvm::any_of(AS->getAttrs(), [](const Attr *A) { return isa<FallThroughAttr>(A); }))
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
    if (const auto *L = dyn_cast<LabelStmt>(S))
      return transfersControl(L->getSubStmt());
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
    // Only comments count here: re-lex the lines and test comment tokens, so an identifier
    // containing "fallthrough" is not an annotation (the attribute is handled by the caller).
    StringRef Buf = SM.getBufferData(FID);
    Lexer Raw(SM.getLocForStartOfFile(FID), LO, Buf.begin(), Buf.data() + SM.getFileOffset(From), Buf.end());
    Raw.SetCommentRetentionState(true);
    static const std::regex Marker("fall[ -]?thr(ough|u)", std::regex::icase);
    unsigned Stop = SM.getFileOffset(To);
    Token Tok;
    while (!Raw.LexFromRawLexer(Tok)) {
      if (SM.getFileOffset(Tok.getLocation()) >= Stop)
        break;
      if (!Tok.is(tok::comment))
        continue;
      StringRef Text = Lexer::getSourceText(CharSourceRange::getCharRange(Tok.getLocation(), Tok.getEndLoc()), SM, LO);
      if (std::regex_search(Text.begin(), Text.end(), Marker))
        return true;
    }
    return false;
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
    F->addMatcher(declStmt(hasAncestor(functionDecl().bind("outer"))).bind("ds"), this);
  }
  void check(const MatchFinder::MatchResult &R) override {
    const auto *DS = R.Nodes.getNodeAs<DeclStmt>("ds");
    for (const Decl *D : DS->decls())
      if (const auto *Fn = dyn_cast<FunctionDecl>(D))
        diag(DS->getBeginLoc(), "Function %0 is declared inside the body of function %1.")
            << identName(Fn) << identName(R.Nodes.getNodeAs<FunctionDecl>("outer"));
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
        << identName(FD) << identName(R.Nodes.getNodeAs<CXXRecordDecl>("class"));
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
    const ValueDecl *L = accessedDecl(Eq->getLHS()->IgnoreParenImpCasts());
    const ValueDecl *Rr = accessedDecl(Eq->getRHS()->IgnoreParenImpCasts());
    if (L && Rr && L == Rr)
      return; // NaN check x != x (CodeQL: same VariableAccess target, fields included)
    diag(Eq->getOperatorLoc(), "Floating-point values are compared with %0; compare against a tolerance instead.")
        << Eq->getOpcodeStr();
  }
};

// cpp/fprime/float-loop-control
class FloatLoopControlCheck : public ClangTidyCheck {
public:
  using ClangTidyCheck::ClangTidyCheck;
  void registerMatchers(MatchFinder *F) override {
    auto FloatVar = expr(anyOf(declRefExpr(to(varDecl(hasType(realFloatingPointType())))),
                               memberExpr(member(fieldDecl(hasType(realFloatingPointType()))))))
                        .bind("var");
    F->addMatcher(forStmt(hasCondition(forEachDescendant(FloatVar))).bind("loop"), this);
    F->addMatcher(forStmt(hasCondition(FloatVar)).bind("loop"), this);
    F->addMatcher(whileStmt(hasCondition(anyOf(FloatVar, forEachDescendant(FloatVar)))).bind("loop"), this);
    F->addMatcher(doStmt(hasCondition(anyOf(FloatVar, forEachDescendant(FloatVar)))).bind("loop"), this);
  }
  void check(const MatchFinder::MatchResult &R) override {
    const auto *Loop = R.Nodes.getNodeAs<Stmt>("loop");
    const ValueDecl *Var = accessedDecl(R.Nodes.getNodeAs<Expr>("var"));
    auto Key = std::make_pair(Loop, static_cast<const Decl *>(Var));
    if (!Seen.insert(Key).second)
      return;
    diag(Loop->getBeginLoc(), "This loop's condition depends on the floating-point variable %0.") << Var->getName();
  }

private:
  std::set<std::pair<const Stmt *, const Decl *>> Seen;
};

// cpp/fprime/global-mutable-variable
class GlobalMutableVariableCheck : public ClangTidyCheck {
public:
  using ClangTidyCheck::ClangTidyCheck;
  void registerMatchers(MatchFinder *F) override {
    F->addMatcher(varDecl(unless(parmVarDecl()), hasGlobalStorage(), unless(isConstexpr()),
                          unless(hasType(isConstQualified())))
                      .bind("var"),
                  this);
  }
  void check(const MatchFinder::MatchResult &R) override {
    const auto *V = R.Nodes.getNodeAs<VarDecl>("var");
    if (V->isStaticDataMember() || V->isImplicit())
      return;
    if (const VarDecl *Def = V->getDefinition()) {
      if (Def != V)
        return;
    } else if (V != V->getCanonicalDecl()) {
      return;
    }
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
    F->addMatcher(conditionalOperator(anyOf(hasType(realFloatingPointType()), hasTrueExpression(Float),
                                            hasFalseExpression(Float)),
                                      hasAncestor(functionDecl().bind("fn")))
                      .bind("op"),
                  this);
  }
  void check(const MatchFinder::MatchResult &R) override {
    const auto *Fn = R.Nodes.getNodeAs<FunctionDecl>("fn");
    if (!isIsrFunction(Fn))
      return;
    const auto *Op = R.Nodes.getNodeAs<Expr>("op");
    if (const auto *BO = dyn_cast<BinaryOperator>(Op))
      if (BO->getOpcode() == BO_Comma)
        return; // CodeQL's CommaExpr is not an Operation
    diag(Op->getExprLoc(), "Floating-point operation in interrupt service routine %0.") << identName(Fn);
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
    visit(Fn->getBody(), HasLoop, Count);
    // CodeQL synthesises a ReturnStmt when control can fall off the end of the body.
    if (const auto *Body = dyn_cast<CompoundStmt>(Fn->getBody()))
      if (Body->body_empty() || !isa<ReturnStmt>(Body->body_back()))
        ++Count;
    if (HasLoop)
      diag(Fn->getLocation(), "Interrupt service routine %0 contains a loop; keep ISRs minimal.") << identName(Fn);
    else if (Count > 20)
      diag(Fn->getLocation(), "Interrupt service routine %0 has %1 statements; keep ISRs minimal.")
          << identName(Fn) << Count;
  }

private:
  // Mirrors CodeQL's count(Stmt s | s.getEnclosingFunction() = isr and not s instanceof BlockStmt):
  // every statement-position node counts once (clang has no ExprStmt node, so an Expr in
  // statement position counts as one), blocks do not, expressions are not descended into
  // (lambdas and local classes belong to other functions), and a switch carries one extra
  // synthetic statement in CodeQL's model.
  static void visit(const Stmt *S, bool &HasLoop, unsigned &Count) {
    if (!S)
      return;
    if (const auto *Block = dyn_cast<CompoundStmt>(S)) {
      for (const Stmt *C : Block->body())
        visit(C, HasLoop, Count);
      return;
    }
    ++Count;
    if (const auto *If = dyn_cast<IfStmt>(S)) {
      visit(If->getThen(), HasLoop, Count);
      visit(If->getElse(), HasLoop, Count);
    } else if (const auto *Sw = dyn_cast<SwitchStmt>(S)) {
      ++Count;
      visit(Sw->getBody(), HasLoop, Count);
    } else if (const auto *For = dyn_cast<ForStmt>(S)) {
      HasLoop = true;
      visit(For->getInit(), HasLoop, Count);
      visit(For->getBody(), HasLoop, Count);
    } else if (const auto *W = dyn_cast<WhileStmt>(S)) {
      HasLoop = true;
      visit(W->getBody(), HasLoop, Count);
    } else if (const auto *D = dyn_cast<DoStmt>(S)) {
      HasLoop = true;
      visit(D->getBody(), HasLoop, Count);
    } else if (const auto *R = dyn_cast<CXXForRangeStmt>(S)) {
      HasLoop = true;
      visit(R->getBody(), HasLoop, Count);
    } else if (const auto *Case = dyn_cast<SwitchCase>(S)) {
      visit(Case->getSubStmt(), HasLoop, Count);
    } else if (const auto *L = dyn_cast<LabelStmt>(S)) {
      visit(L->getSubStmt(), HasLoop, Count);
    } else if (const auto *A = dyn_cast<AttributedStmt>(S)) {
      visit(A->getSubStmt(), HasLoop, Count);
    }
  }
};

// cpp/fprime/incomplete-code -- comment markers via the preprocessor's comment handler.
class IncompleteCodeCheck : public ClangTidyCheck, public CommentHandler {
  class SkippedRanges : public PPCallbacks {
  public:
    SkippedRanges(IncompleteCodeCheck &C, Preprocessor &PP) : Check(C), PP(PP) {}
    // The comment handler never sees #if 0 ... #endif; re-lex those ranges for comments.
    void SourceRangeSkipped(SourceRange Range, SourceLocation) override {
      const SourceManager &SM = PP.getSourceManager();
      if (!fromSource(Range.getBegin(), SM))
        return;
      std::pair<FileID, unsigned> B = SM.getDecomposedLoc(Range.getBegin());
      std::pair<FileID, unsigned> E = SM.getDecomposedLoc(Range.getEnd());
      if (B.first != E.first)
        return;
      StringRef Buf = SM.getBufferData(B.first);
      Lexer Raw(SM.getLocForStartOfFile(B.first), PP.getLangOpts(), Buf.begin(), Buf.data() + B.second, Buf.end());
      Raw.SetCommentRetentionState(true);
      Token Tok;
      while (!Raw.LexFromRawLexer(Tok)) {
        if (SM.getDecomposedLoc(Tok.getLocation()).second >= E.second)
          break;
        if (Tok.is(tok::comment))
          Check.report(SourceRange(Tok.getLocation(), Tok.getEndLoc()), SM, PP.getLangOpts());
      }
    }

  private:
    IncompleteCodeCheck &Check;
    Preprocessor &PP;
  };

public:
  using ClangTidyCheck::ClangTidyCheck;
  void registerPPCallbacks(const SourceManager &, Preprocessor *PP, Preprocessor *) override {
    PP->addCommentHandler(this);
    PP->addPPCallbacks(std::make_unique<SkippedRanges>(*this, *PP));
  }
  bool HandleComment(Preprocessor &PP, SourceRange Range) override {
    report(Range, PP.getSourceManager(), PP.getLangOpts());
    return false;
  }
  void report(SourceRange Range, const SourceManager &SM, const LangOptions &LO) {
    if (!fromSource(Range.getBegin(), SM))
      return;
    StringRef Text = Lexer::getSourceText(CharSourceRange::getCharRange(Range), SM, LO);
    static const std::regex Marker("\\b(TBD|TODO|FIXME)\\b");
    std::cmatch M;
    if (std::regex_search(Text.begin(), Text.end(), M, Marker))
      diag(Range.getBegin(), "Comment contains the incomplete-code marker %0.") << std::string(M[1]);
  }
};

// cpp/fprime/define-constant -- numeric object-like macros.
class DefineConstantCheck : public ClangTidyCheck {
  class Callbacks : public PPCallbacks {
  public:
    Callbacks(DefineConstantCheck &C, const SourceManager &SM, const LangOptions &LO) : Check(C), SM(SM), LO(LO) {}
    void MacroDefined(const Token &Name, const MacroDirective *MD) override {
      const MacroInfo *MI = MD->getMacroInfo();
      if (!MI || MI->isBuiltinMacro() || !fromSource(MI->getDefinitionLoc(), SM))
        return;
      // `#define F() 1` is a function-like macro, not a constant definition; CodeQL's body regex
      // reports it anyway (a CodeQL defect we deliberately do not mirror).
      if (MI->isFunctionLike())
        return;
      StringRef Path = SM.getFilename(MI->getDefinitionLoc());
      if (Path.empty() || Path.startswith("<")) // -D on the command line, <built-in>
        return;
      if (Path.contains("/config/") || inTestPath(Path) || Path.endswith("Ac.hpp") || Path.endswith("Ac.cpp"))
        return;
      std::string Body;
      for (const Token &T : MI->tokens()) {
        if (T.hasLeadingSpace() && !Body.empty())
          Body += ' ';
        Body += Lexer::getSpelling(T, SM, LO);
      }
      static const std::regex Numeric(
          "^\\(?\\s*[-+]?(0[xX][0-9a-fA-F]+|[0-9]+(\\.[0-9]*)?([eE][-+]?[0-9]+)?)[uUlLfF]*\\s*\\)?$");
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
