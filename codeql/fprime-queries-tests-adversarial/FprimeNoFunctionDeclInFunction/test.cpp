int ext(int);
void outer() {
  int f(int);
  int g(), h();
  extern int e(double);
  struct Local { int method(); void other(); };
  using ::ext;
  typedef int (*FnPtr)(int);
  FnPtr p = nullptr;
  int (*q)(int) = f;
  (void)p; (void)q;
  auto lam = [] { void inner(); inner(); };
  lam();
}
template <typename T> void tmpl() { T tfn(T); }
void inst() { tmpl<int>(); }
namespace ns { void nsFn(); }
