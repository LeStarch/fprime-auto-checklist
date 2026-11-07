typedef unsigned int U32;
typedef float F32;
typedef double F64;
namespace Fw { void SwAssert(int); }
#define FW_ASSERT(cond, ...) ((cond) ? static_cast<void>(0) : Fw::SwAssert(__LINE__))
F32 fabsf(F32);
struct S { F32 scale; };
F32 cases(F32 x, F32 f, F64 d, U32 i, S s, bool c) {
  F32 r = x / f;
  if (f != 0.0f) { r += x / f; }
  if (fabsf(f) > 1e-6f) { r += x / f; }
  r += c ? x / f : 0.0f;
  r += (f > 0.0f) ? x / f : 0.0f;
  r /= f;
  r += static_cast<F32>(i / d);
  r += x / 2.0f;
  const F32 k = 2.0f;
  r += x / k;
  const F32 z = 0.0f;
  r += x / z;
  r += x / s.scale;
  if (s.scale != 0.0f) { r += x / s.scale; }
  r += static_cast<F32>(x / (F64)f);
  r += x / (f);
  r += static_cast<F32>(i / f);
  while (f > 1.0f) { r += x / f; f -= 1.0f; }
  return r;
}
F32 afterGuard(F32 x, F32 f) {
  F32 r = x / f;
  FW_ASSERT(f != 0.0f);
  return r + x / f;
}
F32 unrelatedGuard(F32 x, F32 f, F32 g) {
  if (g != 0.0f) { return x / f; }
  return 0.0f;
}
template <typename T> T tmpl(T a, T b) { return a / b; }
F32 inst() { return tmpl(1.0f, 2.0f) + static_cast<F32>(tmpl(1u, 2u)); }
U32 intDivision(U32 a, U32 b) { return a / b; }
