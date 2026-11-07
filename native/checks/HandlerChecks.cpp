// Checks about F Prime handler functions, ISRs and critical regions.
#include "FprimeChecks.h"

#include "clang/AST/ParentMapContext.h"
#include "clang/Analysis/CFG.h"

using namespace clang::ast_matchers;

namespace clang {
namespace tidy {
namespace fprime {

namespace {

bool isMemberCall(const CallExpr *C) { return isa_and_nonnull<CXXMethodDecl>(C->getDirectCallee()); }

StringRef calleeName(const CallExpr *C) {
  const FunctionDecl *F = C->getDirectCallee();
  return F && F->getDeclName().isIdentifier() ? F->getName() : StringRef();
}

std::string declaringTypeName(const CallExpr *C) {
  if (const auto *M = dyn_cast_or_null<CXXMethodDecl>(C->getDirectCallee()))
    return M->getParent()->getQualifiedNameAsString();
  return "";
}

bool isBlockingCall(const CallExpr *C) {
  static const char *Free[] = {"sleep",    "usleep",   "nanosleep",     "sem_wait", "sem_timedwait", "pthread_mutex_lock",
                               "pthread_cond_wait", "pthread_cond_timedwait", "pthread_join", "select", "poll",
                               "epoll_wait", "accept", "connect", "recv", "recvfrom", "recvmsg", "send", "sendto",
                               "sendmsg", "flock", "fsync", "msgrcv", "mq_receive"};
  static const char *Member[] = {"lock", "lockData", "take", "wait", "delay", "receiveBlocking"};
  StringRef Name = calleeName(C);
  if (Name.empty())
    return false;
  if (!isMemberCall(C))
    return llvm::is_contained(Free, Name);
  return StringRef(declaringTypeName(C)).startswith("Os::") && llvm::is_contained(Member, Name);
}

bool isFileAccessCall(const CallExpr *C) {
  static const char *Free[] = {"open",   "openat", "close",  "read",   "write",  "pread", "pwrite", "lseek",  "fopen",
                               "fclose", "fread",  "fwrite", "fgets",  "fputs",  "fprintf", "fscanf", "fseek", "fflush",
                               "stat",   "fstat",  "unlink", "rename", "mkdir",  "rmdir", "sync",   "fsync"};
  static const char *Member[] = {"open", "close", "read", "write", "seek", "flush", "crc32", "readDirectory"};
  StringRef Name = calleeName(C);
  if (Name.empty())
    return false;
  if (!isMemberCall(C))
    return llvm::is_contained(Free, Name);
  std::string Type = declaringTypeName(C);
  return (Type == "Os::File" || Type == "Os::Directory" || Type == "Os::FileSystem") && llvm::is_contained(Member, Name);
}

bool isLockCall(const CallExpr *C) {
  StringRef Name = calleeName(C);
  if (isMemberCall(C))
    return Name == "lock" || Name == "lockData" || Name == "take";
  return Name == "pthread_mutex_lock" || Name == "sem_wait";
}
bool isUnlockCall(const CallExpr *C) {
  StringRef Name = calleeName(C);
  if (isMemberCall(C))
    return Name == "unLock" || Name == "unlock" || Name == "unlockData" || Name == "release";
  return Name == "pthread_mutex_unlock" || Name == "sem_post";
}

} // namespace

// cpp/fprime/internal-port-invoke
class InternalPortInvokeCheck : public ClangTidyCheck {
public:
  using ClangTidyCheck::ClangTidyCheck;
  void registerMatchers(MatchFinder *F) override {
    F->addMatcher(callExpr(callee(functionDecl(matchesName("_internalInterfaceHandler$")).bind("target")),
                           hasAncestor(functionDecl().bind("caller")))
                      .bind("call"),
                  this);
  }
  void check(const MatchFinder::MatchResult &R) override {
    const auto *Caller = R.Nodes.getNodeAs<FunctionDecl>("caller");
    if (Caller->getNameAsString().find("ispatch") != std::string::npos ||
        StringRef(Caller->getNameAsString()).lower().find("dispatch") != std::string::npos)
      return;
    const auto *Target = R.Nodes.getNodeAs<FunctionDecl>("target");
    std::string Name = Target->getNameAsString();
    std::string Invoke = Name.substr(0, Name.size() - strlen("_internalInterfaceHandler")) + "_internalInterfaceInvoke";
    const auto *Call = R.Nodes.getNodeAs<CallExpr>("call");
    diag(Call->getExprLoc(), "Internal port handler %0 is called directly; use the auto-coded %1 instead.")
        << Name << Invoke;
  }
};

// cpp/fprime/validation-failure-return
class ValidationFailureReturnCheck : public ClangTidyCheck {
public:
  using ClangTidyCheck::ClangTidyCheck;
  void registerMatchers(MatchFinder *F) override {
    F->addMatcher(ifStmt(unless(hasElse(stmt())), hasThen(stmt().bind("then")),
                         hasAncestor(functionDecl().bind("handler")))
                      .bind("if"),
                  this);
  }
  void check(const MatchFinder::MatchResult &R) override {
    const auto *Handler = R.Nodes.getNodeAs<FunctionDecl>("handler");
    if (!isFprimeHandler(Handler->getName()))
      return;
    const auto *Then = R.Nodes.getNodeAs<Stmt>("then");
    if (!hasFailureReport(Then) || transfersControl(Then))
      return;
    const auto *If = R.Nodes.getNodeAs<IfStmt>("if");
    if (!hasFollowingStmt(If, *R.Context, Handler))
      return;
    if (!Seen.insert(If).second)
      return;
    diag(If->getBeginLoc(), "This validation-failure branch in handler %0 sends a failed command response but does not return.")
        << Handler->getName();
  }

private:
  std::set<const Stmt *> Seen;
  // cmdResponse_out(_, _, <non-OK>) somewhere in S, not inside a lambda (another function).
  static bool hasFailureReport(const Stmt *S) {
    if (!S || isa<LambdaExpr>(S))
      return false;
    if (const auto *C = dyn_cast<CallExpr>(S))
      if (const FunctionDecl *F = C->getDirectCallee();
          F && F->getName() == "cmdResponse_out" && C->getNumArgs() >= 3 && isFailureResponse(C->getArg(2)))
        return true;
    for (const Stmt *Child : S->children())
      if (hasFailureReport(Child))
        return true;
    return false;
  }
  // CodeQL's Stmt.getFollowingStmt(): the next statement of the enclosing block, looking through
  // case/default labels; the last statement of a function body is followed by the synthesised return.
  static bool hasFollowingStmt(const Stmt *S, ASTContext &Ctx, const FunctionDecl *Fn) {
    const Stmt *Cur = S;
    while (true) {
      const auto &Parents = Ctx.getParents(*Cur);
      if (Parents.empty())
        return false;
      const Stmt *P = Parents[0].get<Stmt>();
      if (!P)
        return false;
      if (isa<SwitchCase>(P) || isa<LabelStmt>(P) || isa<AttributedStmt>(P)) {
        Cur = P;
        continue;
      }
      const auto *Block = dyn_cast<CompoundStmt>(P);
      if (!Block)
        return false;
      if (Block->body_back() != Cur)
        return true;
      return Block == Fn->getBody();
    }
  }
  static void enumConstants(const Stmt *S, bool &NonOk, bool &Ok) {
    if (!S)
      return;
    if (const auto *DRE = dyn_cast<DeclRefExpr>(S))
      if (const auto *EC = dyn_cast<EnumConstantDecl>(DRE->getDecl()))
        (EC->getName() == "OK" ? Ok : NonOk) = true;
    for (const Stmt *C : S->children())
      enumConstants(C, NonOk, Ok);
  }
  static bool isFailureResponse(const Expr *Arg) {
    bool NonOk = false, Ok = false;
    enumConstants(Arg, NonOk, Ok);
    return NonOk && !Ok;
  }
  static bool transfersControl(const Stmt *S) {
    if (!S)
      return false;
    if (isa<ReturnStmt>(S) || isa<BreakStmt>(S) || isa<ContinueStmt>(S))
      return true;
    if (const auto *B = dyn_cast<CompoundStmt>(S))
      return !B->body_empty() && transfersControl(B->body_back());
    return false;
  }
};

// cpp/fprime/no-blocking-in-isr and cpp/fprime/no-blocking-in-sched-handler
class NoBlockingCheckBase : public ClangTidyCheck {
public:
  using ClangTidyCheck::ClangTidyCheck;
  void registerMatchers(MatchFinder *F) override {
    F->addMatcher(callExpr(hasAncestor(functionDecl().bind("fn"))).bind("call"), this);
  }
  void check(const MatchFinder::MatchResult &R) override {
    const auto *Call = R.Nodes.getNodeAs<CallExpr>("call");
    const auto *Fn = R.Nodes.getNodeAs<FunctionDecl>("fn");
    if (!isBlockingCall(Call) || !applies(Fn))
      return;
    diag(Call->getExprLoc(), "Blocking operation %0 called in %1 %2.") << calleeName(Call) << contextKind() << Fn->getName();
  }
  virtual bool applies(const FunctionDecl *Fn) const = 0;
  virtual StringRef contextKind() const = 0;
};
class NoBlockingInIsrCheck : public NoBlockingCheckBase {
public:
  using NoBlockingCheckBase::NoBlockingCheckBase;
  bool applies(const FunctionDecl *Fn) const override { return isIsrFunction(Fn); }
  StringRef contextKind() const override { return "interrupt service routine"; }
};
class NoBlockingInSchedHandlerCheck : public NoBlockingCheckBase {
public:
  using NoBlockingCheckBase::NoBlockingCheckBase;
  bool applies(const FunctionDecl *Fn) const override { return isSchedInHandler(Fn->getName()); }
  StringRef contextKind() const override { return "rate-group handler"; }
};

// cpp/fprime/no-file-access-in-critical
class NoFileAccessInCriticalCheck : public ClangTidyCheck {
public:
  using ClangTidyCheck::ClangTidyCheck;
  void registerMatchers(MatchFinder *F) override {
    F->addMatcher(functionDecl(isDefinition(), hasBody(stmt())).bind("fn"), this);
  }
  void check(const MatchFinder::MatchResult &R) override {
    const auto *Fn = R.Nodes.getNodeAs<FunctionDecl>("fn");
    llvm::SmallVector<const CallExpr *, 8> FileAccesses, Locks;
    collect(Fn->getBody(), FileAccesses, Locks);
    if (FileAccesses.empty())
      return;
    bool Sched = isSchedInHandler(Fn->getName());
    std::set<const CallExpr *> Locked;
    if (!Sched && !Locks.empty())
      lockedFileAccesses(Fn, *R.Context, Locks, Locked);
    for (const CallExpr *FA : FileAccesses) {
      if (Sched)
        diag(FA->getExprLoc(), "Synchronous file access %0 performed in rate-group handler %1.")
            << calleeName(FA) << Fn->getName();
      else if (Locked.count(FA))
        diag(FA->getExprLoc(), "Synchronous file access %0 performed in a critical region (lock held).") << calleeName(FA);
    }
  }

private:
  static void collect(const Stmt *S, llvm::SmallVectorImpl<const CallExpr *> &FA,
                      llvm::SmallVectorImpl<const CallExpr *> &Locks) {
    if (!S || isa<LambdaExpr>(S))
      return;
    if (const auto *C = dyn_cast<CallExpr>(S)) {
      if (isFileAccessCall(C))
        FA.push_back(C);
      if (isLockCall(C))
        Locks.push_back(C);
    }
    for (const Stmt *Child : S->children())
      collect(Child, FA, Locks);
  }
  // Forward CFG walk from each lock call, stopping at unlock calls; records reached file accesses.
  static void lockedFileAccesses(const FunctionDecl *Fn, ASTContext &Ctx, llvm::ArrayRef<const CallExpr *> Locks,
                                 std::set<const CallExpr *> &Out) {
    CFG::BuildOptions Options;
    Options.setAllAlwaysAdd();
    std::unique_ptr<CFG> G = CFG::buildCFG(Fn, Fn->getBody(), &Ctx, Options);
    if (!G)
      return;
    for (const CallExpr *Lock : Locks) {
      // Locate the lock's block and element index.
      const CFGBlock *Start = nullptr;
      unsigned Index = 0;
      for (const CFGBlock *B : *G) {
        for (unsigned I = 0; I < B->size(); ++I)
          if (auto CS = (*B)[I].getAs<CFGStmt>(); CS && CS->getStmt() == Lock) {
            Start = B;
            Index = I;
          }
        if (Start)
          break;
      }
      if (!Start)
        continue;
      std::set<const CFGBlock *> Visited;
      llvm::SmallVector<std::pair<const CFGBlock *, unsigned>, 16> Work;
      Work.push_back({Start, Index + 1});
      while (!Work.empty()) {
        auto [B, From] = Work.pop_back_val();
        bool Unlocked = false;
        for (unsigned I = From; I < B->size(); ++I) {
          auto CS = (*B)[I].getAs<CFGStmt>();
          if (!CS)
            continue;
          if (const auto *C = dyn_cast<CallExpr>(CS->getStmt())) {
            if (isUnlockCall(C)) {
              Unlocked = true;
              break;
            }
            if (isFileAccessCall(C))
              Out.insert(C);
          }
        }
        if (Unlocked)
          continue;
        for (const CFGBlock *Succ : B->succs())
          if (Succ && Visited.insert(Succ).second)
            Work.push_back({Succ, 0});
      }
    }
  }
};

void registerHandlerChecks(ClangTidyCheckFactories &F) {
  F.registerCheck<InternalPortInvokeCheck>("fprime-internal-port-invoke");
  F.registerCheck<ValidationFailureReturnCheck>("fprime-validation-failure-return");
  F.registerCheck<NoBlockingInIsrCheck>("fprime-no-blocking-in-isr");
  F.registerCheck<NoBlockingInSchedHandlerCheck>("fprime-no-blocking-in-sched-handler");
  F.registerCheck<NoFileAccessInCriticalCheck>("fprime-no-file-access-in-critical");
}

} // namespace fprime
} // namespace tidy
} // namespace clang
