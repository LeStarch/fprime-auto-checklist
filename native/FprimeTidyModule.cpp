// Registers every fprime-* check with clang-tidy (loaded with --load=libFprimeTidy.so).
#include "clang-tidy/ClangTidyModule.h"
#include "clang-tidy/ClangTidyModuleRegistry.h"

#include "checks/FprimeChecks.h"

namespace clang {
namespace tidy {
namespace fprime {

class FprimeModule : public ClangTidyModule {
public:
  void addCheckFactories(ClangTidyCheckFactories &F) override { registerFprimeChecks(F); }
};

} // namespace fprime

static ClangTidyModuleRegistry::Add<fprime::FprimeModule> X("fprime-module", "F Prime component checklist checks.");

// Keep the module's static initializer from being discarded.
volatile int FprimeModuleAnchorSource = 0;

} // namespace tidy
} // namespace clang
