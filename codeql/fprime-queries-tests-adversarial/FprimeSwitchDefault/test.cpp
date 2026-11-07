enum E { A, B };
#define SWITCH_NO_DEFAULT(v) switch (v) { case A: break; case B: break; }
#define DEFAULT_CASE default: break;
int plain(int x) { switch (x) { case 1: return 1; } return 0; }
int nestedInnerHasDefault(int x, int y) {
  switch (x) {
    case 1:
      switch (y) { default: return 2; }
      break;
  }
  return 0;
}
int nestedOuterHasDefault(int x, int y) {
  switch (x) {
    default:
      switch (y) { case 1: return 1; }
  }
  return 0;
}
int defaultInBlock(int x) { switch (x) { case 1: { default: return 3; } } return 0; }
int allEnumValues(E e) { switch (e) { case A: return 0; case B: return 1; } return 2; }
int fromMacro(E e) { SWITCH_NO_DEFAULT(e) return 0; }
int defaultFromMacro(int x) { switch (x) { case 1: break; DEFAULT_CASE } return 0; }
int emptySwitch(int x) { switch (x) {} return 0; }
template <typename T> int tmpl(T v) { switch (v) { case 0: return 0; } return 1; }
int inst() { return tmpl(1) + tmpl(2L); }
int inLambda(int x) { auto f = [x] { switch (x) { case 0: return 0; } return 1; }; return f(); }
int defaultOnly(int x) { switch (x) { default: return 0; } }
