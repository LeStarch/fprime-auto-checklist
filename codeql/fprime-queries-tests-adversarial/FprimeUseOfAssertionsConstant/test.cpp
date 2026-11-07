namespace Fw { void SwAssert(int line); }
#define FW_ASSERT(cond) ((cond) ? static_cast<void>(0) : Fw::SwAssert(__LINE__))
#define assert(cond) ((cond) ? static_cast<void>(0) : Fw::SwAssert(__LINE__))
#define MAX_ITEMS 8
typedef unsigned int U32;
static const U32 LIMIT = 5;
enum Mode { MODE_A, MODE_B };
struct Holder { static const U32 SIZE = 4; enum Kind { K1, K2 }; U32 member; Kind kind; };
void cases(U32 x, Holder h, Holder* hp, bool c) {
  U32 buf[8];
  FW_ASSERT(1 == 1);
  FW_ASSERT(sizeof(int) == 4);
  FW_ASSERT(false);
  FW_ASSERT(0);
  FW_ASSERT(!true);
  FW_ASSERT(x || true);
  FW_ASSERT(LIMIT == 5);
  FW_ASSERT(MODE_A != MODE_B);
  FW_ASSERT(MAX_ITEMS > 0);
  FW_ASSERT(sizeof(buf) >= 8);
  FW_ASSERT(buf != nullptr);
  FW_ASSERT(&x != nullptr);
  FW_ASSERT(Holder::SIZE == 4);
  FW_ASSERT(h.SIZE == 4);
  FW_ASSERT(hp->kind == Holder::K1);
  FW_ASSERT(h.member == 1);
  FW_ASSERT(c ? 1 : 1);
  FW_ASSERT(true && 1);
  FW_ASSERT(x == x);
  FW_ASSERT((x & 0) == 0);
  FW_ASSERT(x * 0 == 0);
  assert(2 > 1);
  FW_ASSERT(1 == 1 && x > 0);
  FW_ASSERT("literal");
  FW_ASSERT(1.5 > 1.0);
  FW_ASSERT('a' == 97);
  FW_ASSERT(nullptr == nullptr);
}
template <unsigned N> void tmpl(U32 x) { FW_ASSERT(N > 0); FW_ASSERT(x < N); }
void inst() { tmpl<4>(1); tmpl<8>(2); }
constexpr U32 cexpr() { return 3; }
void viaConstexpr() { FW_ASSERT(cexpr() == 3); }
