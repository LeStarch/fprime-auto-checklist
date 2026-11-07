// Collects the per-file registration functions into one entry point.
#include "FprimeChecks.h"

namespace clang {
namespace tidy {
namespace fprime {

void registerSyntaxChecks(ClangTidyCheckFactories &F);
void registerAssertionChecks(ClangTidyCheckFactories &F);
void registerHandlerChecks(ClangTidyCheckFactories &F);
void registerGuardChecks(ClangTidyCheckFactories &F);
void registerModelChecks(ClangTidyCheckFactories &F);
void registerStyleChecks(ClangTidyCheckFactories &F);

void registerFprimeChecks(ClangTidyCheckFactories &F) {
  registerSyntaxChecks(F);
  registerAssertionChecks(F);
  registerHandlerChecks(F);
  registerGuardChecks(F);
  registerModelChecks(F);
  registerStyleChecks(F);
}

} // namespace fprime
} // namespace tidy
} // namespace clang
