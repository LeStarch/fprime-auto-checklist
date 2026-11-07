typedef unsigned int U32;
typedef float F32;
namespace Fw { void SwAssert(int); }
#define FW_ASSERT(cond, ...) ((cond) ? static_cast<void>(0) : Fw::SwAssert(__LINE__))
struct S { U32 count; };
void sink(U32);
U32 cases(U32 a, U32 b, U32 c, bool x, S s, S* ps, U32* p, F32 f) {
  U32 r = a / b;
  if (x) { if (b) { sink(1); } }
  r += a / b;
  if (c) { r += a / b; }
  r += a / c;
  if (a != 0) { r += a / b; }
  r += x ? a / b : 0;
  r += (c && b) ? a / c : 0;
  const U32 k = 4;
  r += a / k;
  const U32 z = 0;
  r += a / z;
  r += a / s.count;
  if (s.count) { r += a / s.count; }
  r += a / ps->count;
  r += a / *p;
  r %= b;
  r /= b;
  r += a / (b);
  r += a / (U32)b;
  r += a / static_cast<U32>(b);
  for (; b != 0; --b) { r += a / b; }
  if (b) { b = 0; }
  r += a / b;
  r += (U32)(1.0f / f);
  auto l = [a, b] { return a / b; };
  r += l();
  return r;
}
U32 afterGuard(U32 a, U32 b) {
  U32 r = a / b;
  FW_ASSERT(b != 0);
  return r + a / b;
}
U32 loopGuard(U32 a, U32 b) {
  U32 r = 0;
  while (b > 0) { r += a / b; b--; }
  return r;
}
template <typename T> T tmpl(T a, T b) { return a / b; }
U32 inst() { return tmpl(1u, 2u) + static_cast<U32>(tmpl(1.0f, 2.0f)); }
U32 assertNonZero(U32 a, U32 b) { FW_ASSERT(b > 1); return a / b; }
U32 switchGuard(U32 a, U32 b) { switch (b) { default: return a / b; } }
U32 doGuard(U32 a, U32 b) { U32 r = 0; do { r += a / b; } while (b--); return r; }
