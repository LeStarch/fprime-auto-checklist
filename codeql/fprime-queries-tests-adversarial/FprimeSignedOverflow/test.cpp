typedef int I32;
typedef short I16;
typedef signed char I8;
typedef long long I64;
typedef unsigned int U32;
typedef unsigned char U8;
namespace Fw { void SwAssert(int); }
#define FW_ASSERT(cond, ...) ((cond) ? static_cast<void>(0) : Fw::SwAssert(__LINE__))
struct S { I32 a; I32 b; };
I64 cases(I32 a, I32 b, I16 s1, I16 s2, I8 c1, I8 c2, U32 u, U8 small, I64 big, S s, char ch) {
  I64 r = 0;
  r += a + b;
  r += a - b;
  r += a * b;
  r += a + 1;
  r += 2 * a;
  r += u + a;
  r += s1 + s2;
  r += s1 * s2;
  r += c1 + c2;
  r += small + a;
  r += big + a;
  r += static_cast<I64>(a) + b;
  r += static_cast<I64>(a) * b;
  r += (I64)a + (I64)b;
  r += ch + ch;
  I32 x = 5;
  r += x + a;
  I32 y = 7;
  r += x + y;
  for (I32 i = 0; i < 10; i++) { r += i + a; r += i + x; }
  if (a < 100 && b < 100) { r += a + b; }
  if (a < 100) { r += a * b; }
  FW_ASSERT(b > -1000);
  r += a - b;
  r += s.a + s.b;
  if (s.a < 10) { r += s.a + s.b; }
  r += a + b + a;
  I64 wide = a + b;
  r += wide;
  r += (a + b) * 2;
  I32 z = a;
  r += z + b;
  r += u * 2u;
  auto l = [a, b] { return a + b; };
  r += l();
  return r;
}
template <typename T> T tmpl(T a, T b) { return a + b; }
I32 inst() { return tmpl(1, 2) + static_cast<I32>(tmpl(1u, 2u)); }
I32 bounded(I32 a) {
  if (a > 1000) { return 0; }
  if (a < -1000) { return 0; }
  return a * 4;
}
I32 member(S s) { return s.a - s.b; }
