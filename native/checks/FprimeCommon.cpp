// Helpers shared by the fprime-* checks.
#include "FprimeChecks.h"

#include "clang/AST/ParentMapContext.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Analysis/Analyses/Dominators.h"
#include "clang/Analysis/CFG.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"

#include <regex>

namespace clang {
namespace tidy {
namespace fprime {

bool isIsrName(llvm::StringRef Name) {
  static const std::regex Pattern("(^|.*_)isr(_.*|$)|.*interrupt_?handler.*|(^|.*_)irq(_.*|$)");
  return std::regex_match(Name.lower(), Pattern);
}

bool isIsrFunction(const FunctionDecl *F) {
  if (!F)
    return false;
  if (F->getDeclName().isIdentifier() && isIsrName(F->getName()))
    return true;
  for (const Attr *A : F->attrs()) {
    if (llvm::StringRef(A->getSpelling()) == "interrupt")
      return true;
  }
  return false;
}

bool inTestPath(llvm::StringRef Path) { return Path.contains("/test/") || Path.contains("/ut/"); }

const Expr *assertedCondition(const Expr *MacroExpr) {
  const Expr *E = MacroExpr ? MacroExpr->IgnoreParenImpCasts() : nullptr;
  if (const auto *Cond = dyn_cast_or_null<ConditionalOperator>(E))
    return Cond->getCond()->IgnoreParenImpCasts();
  return nullptr;
}

std::string sourceText(const Expr *E, const ASTContext &Ctx) {
  if (!E)
    return "";
  CharSourceRange Range = CharSourceRange::getTokenRange(E->getSourceRange());
  return Lexer::getSourceText(Range, Ctx.getSourceManager(), Ctx.getLangOpts()).str();
}

bool fromSource(SourceLocation Loc, const SourceManager &SM) {
  if (Loc.isInvalid())
    return false;
  SourceLocation Spelling = SM.getExpansionLoc(Loc);
  return !SM.isInSystemHeader(Spelling);
}

namespace {

// Collects every DeclRefExpr to V that is part of a condition or an asserted expression.
class GuardCollector : public RecursiveASTVisitor<GuardCollector> {
public:
  GuardCollector(const VarDecl *V, const SourceManager &SM) : V(V), SM(SM) {}

  llvm::SmallVector<const Stmt *, 8> Guards;

  bool VisitIfStmt(IfStmt *S) { return addIfReads(S->getCond()); }
  bool VisitWhileStmt(WhileStmt *S) { return addIfReads(S->getCond()); }
  bool VisitDoStmt(DoStmt *S) { return addIfReads(S->getCond()); }
  bool VisitForStmt(ForStmt *S) { return addIfReads(S->getCond()); }
  bool VisitConditionalOperator(ConditionalOperator *S) { return addIfReads(S->getCond()); }
  bool VisitSwitchStmt(SwitchStmt *S) { return addIfReads(S->getCond()); }
  bool VisitBinaryOperator(BinaryOperator *B) {
    // Short-circuit operands are conditions of their own.
    if (B->isLogicalOp()) {
      addIfReads(B->getLHS());
      addIfReads(B->getRHS());
    }
    return true;
  }

private:
  bool addIfReads(const Expr *Cond) {
    if (Cond && reads(Cond))
      Guards.push_back(Cond);
    return true;
  }
  bool reads(const Stmt *S) const {
    if (const auto *DRE = dyn_cast<DeclRefExpr>(S))
      return DRE->getDecl() == V;
    for (const Stmt *Child : S->children())
      if (Child && reads(Child))
        return true;
    return false;
  }
  const VarDecl *V;
  const SourceManager &SM;
};

const CFGBlock *blockOf(const CFG &G, const Stmt *S, unsigned *Index) {
  for (const CFGBlock *B : G) {
    unsigned I = 0;
    for (const CFGElement &E : *B) {
      if (auto CS = E.getAs<CFGStmt>()) {
        if (CS->getStmt() == S) {
          if (Index)
            *Index = I;
          return B;
        }
      }
      ++I;
    }
    if (B->getTerminatorStmt() == S || B->getTerminatorCondition(false) == S) {
      if (Index)
        *Index = B->size();
      return B;
    }
  }
  return nullptr;
}

const CFGBlock *blockOfOrParent(const CFG &G, const Stmt *S, ASTContext &Ctx, unsigned *Index) {
  const Stmt *Cur = S;
  while (Cur) {
    if (const CFGBlock *B = blockOf(G, Cur, Index))
      return B;
    const auto &Parents = Ctx.getParents(*Cur);
    if (Parents.empty())
      return nullptr;
    Cur = Parents[0].get<Stmt>();
  }
  return nullptr;
}

} // namespace

bool guardedBefore(const VarDecl *V, const Stmt *Use, const FunctionDecl *F, ASTContext &Ctx) {
  if (!V || !Use || !F || !F->hasBody())
    return false;
  GuardCollector Collector(V, Ctx.getSourceManager());
  Collector.TraverseStmt(F->getBody());
  if (Collector.Guards.empty())
    return false;
  // Also honour FW_ASSERT(cond) by treating the conditional operator's condition as a guard;
  // the collector already records ConditionalOperator conditions, which covers the macro expansion.
  CFG::BuildOptions Options;
  Options.AddImplicitDtors = false;
  std::unique_ptr<CFG> G = CFG::buildCFG(F, F->getBody(), &Ctx, Options);
  if (!G)
    return false;
  unsigned UseIndex = 0;
  const CFGBlock *UseBlock = blockOfOrParent(*G, Use, Ctx, &UseIndex);
  if (!UseBlock)
    return false;
  CFGDominatorTreeImpl<false> Dom;
  AnalysisDeclContextManager Mgr(Ctx);
  AnalysisDeclContext *ADC = Mgr.getContext(F);
  Dom.buildDominatorTree(ADC->getCFG());
  // Build the dominator tree on our own CFG instead (same function, same options).
  CFGDominatorTreeImpl<false> OwnDom;
  OwnDom.buildDominatorTree(G.get());
  for (const Stmt *Guard : Collector.Guards) {
    unsigned GuardIndex = 0;
    const CFGBlock *GuardBlock = blockOfOrParent(*G, Guard, Ctx, &GuardIndex);
    if (!GuardBlock)
      continue;
    if (GuardBlock == UseBlock) {
      if (GuardIndex < UseIndex)
        return true;
      continue;
    }
    if (OwnDom.dominates(GuardBlock, UseBlock))
      return true;
  }
  return false;
}

} // namespace fprime
} // namespace tidy
} // namespace clang
