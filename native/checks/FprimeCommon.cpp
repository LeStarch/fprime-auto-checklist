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

// Collects every access to V that is part of a condition or an asserted expression
// (CodeQL's checkingAccess): the guard node is the access itself.
class GuardCollector : public RecursiveASTVisitor<GuardCollector> {
public:
  explicit GuardCollector(const ValueDecl *V) : V(V) {}

  llvm::SmallVector<const Expr *, 8> Guards;

  bool VisitIfStmt(IfStmt *S) { return addIfReads(S->getCond()); }
  bool VisitWhileStmt(WhileStmt *S) { return addIfReads(S->getCond()); }
  bool VisitDoStmt(DoStmt *S) { return addIfReads(S->getCond()); }
  bool VisitForStmt(ForStmt *S) { return addIfReads(S->getCond()); }
  bool VisitConditionalOperator(ConditionalOperator *S) {
    return addIfReads(S->getCond());
  }
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
    if (Cond)
      collectReads(Cond);
    return true;
  }
  void collectReads(const Stmt *S) {
    if (const auto *E = dyn_cast<Expr>(S))
      if (accessedDecl(E) == V && !llvm::is_contained(Guards, E))
        Guards.push_back(E);
    for (const Stmt *Child : S->children())
      if (Child)
        collectReads(Child);
  }
  const ValueDecl *V;
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
  DynTypedNode Cur = DynTypedNode::create(*S);
  while (true) {
    if (const Stmt *CurStmt = Cur.get<Stmt>())
      if (const CFGBlock *B = blockOf(G, CurStmt, Index))
        return B;
    const auto &Parents = Ctx.getParents(Cur);
    if (Parents.empty())
      return nullptr;
    Cur = Parents[0]; // walks through VarDecl initialisers as well as statements
  }
}

} // namespace

const ValueDecl *accessedDecl(const Expr *E) {
  if (!E)
    return nullptr;
  if (const auto *DRE = dyn_cast<DeclRefExpr>(E))
    return DRE->getDecl();
  if (const auto *ME = dyn_cast<MemberExpr>(E))
    return ME->getMemberDecl();
  return nullptr;
}

const FunctionDecl *enclosingFunction(const Stmt *S, ASTContext &Ctx) {
  DynTypedNode Cur = DynTypedNode::create(*S);
  while (true) {
    const auto &Parents = Ctx.getParents(Cur);
    if (Parents.empty())
      return nullptr;
    if (const auto *F = Parents[0].get<FunctionDecl>())
      return F;
    Cur = Parents[0];
  }
}

bool isConstantValue(const Expr *E, ASTContext &Ctx) {
  if (!E || E->isValueDependent())
    return false;
  Expr::EvalResult Result;
  return E->EvaluateAsRValue(Result, Ctx) && (Result.Val.isInt() || Result.Val.isFloat());
}

bool isCheckingAccess(const ValueDecl *V, const Expr *Access, const FunctionDecl *F, ASTContext &Ctx) {
  if (!F || !F->hasBody())
    return false;
  GuardCollector Collector(V);
  Collector.TraverseStmt(F->getBody());
  return llvm::is_contained(Collector.Guards, Access);
}

bool guardedBefore(const ValueDecl *V, const Stmt *Use, const FunctionDecl *F, ASTContext &Ctx, bool Strict) {
  if (!V || !Use || !F || !F->hasBody())
    return false;
  GuardCollector Collector(V);
  Collector.TraverseStmt(F->getBody());
  if (Collector.Guards.empty())
    return false;
  CFG::BuildOptions Options;
  Options.setAllAlwaysAdd();
  std::unique_ptr<CFG> G = CFG::buildCFG(F, F->getBody(), &Ctx, Options);
  if (!G)
    return false;
  unsigned UseIndex = 0;
  const CFGBlock *UseBlock = blockOfOrParent(*G, Use, Ctx, &UseIndex);
  if (!UseBlock)
    return false;
  CFGDominatorTreeImpl<false> Dom;
  Dom.buildDominatorTree(G.get());
  for (const Expr *Guard : Collector.Guards) {
    unsigned GuardIndex = 0;
    const CFGBlock *GuardBlock = blockOfOrParent(*G, Guard, Ctx, &GuardIndex);
    if (!GuardBlock)
      continue;
    if (GuardBlock == UseBlock) {
      if (!Strict || GuardIndex < UseIndex)
        return true;
      continue;
    }
    if (Dom.dominates(GuardBlock, UseBlock))
      return true;
  }
  return false;
}

} // namespace fprime
} // namespace tidy
} // namespace clang
