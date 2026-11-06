// Collects the per-file registration functions into one entry point.
#include "FprimeChecks.h"

namespace clang {
namespace tidy {
namespace fprime {

void registerSyntaxChecks(ClangTidyCheckFactories &F);
void registerAssertionChecks(ClangTidyCheckFactories &F);
void registerHandlerChecks(ClangTidyCheckFactories &F);

void registerFprimeChecks(ClangTidyCheckFactories &F) {
  registerSyntaxChecks(F);
  registerAssertionChecks(F);
  registerHandlerChecks(F);
}

} // namespace fprime
} // namespace tidy
} // namespace clang
