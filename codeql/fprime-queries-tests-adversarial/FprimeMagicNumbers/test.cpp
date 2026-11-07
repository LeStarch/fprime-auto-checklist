typedef unsigned int U32;
typedef float F32;
#define LIMIT 42
#define SCALE(x) ((x) * 37)
enum E { A = 5, B = 17 << 2 };
static const U32 g_c = 99;
U32 g_nc = 98;
constexpr U32 g_ce = 97;
void sink(U32); void sinkf(F32);
struct S { static const U32 K = 96; U32 m = 95; S() : m(94) {} };
template <typename T> T tmpl(T t) { return t + 93; }
void cases(U32 x) {
  const U32 lc = 92;
  constexpr U32 lce = 91;
  U32 ln = 90;
  static U32 ls = 89;
  static const U32 lsc = 88;
  sink(0); sink(1); sink(-1); sink(2); sink(1024); sink(4096); sink(8192);
  sink(255); sink(65535); sink(4294967295u); sink(0xFF); sink(0x1F); sink(077); sink(0b101);
  sink(10); sink(1000000000); sink(10000000000ull); sink(42);
  sink(x << 8); sink(x >> 24); sink(x << 7); sink(x << 48);
  sinkf(1.0f); sinkf(10.0f); sinkf(20.0); sinkf(0.5f); sinkf(3.14f); sinkf(1e3f); sinkf(1.5e2); sinkf(100.0f);
  sink(LIMIT); sink(SCALE(x)); sink(A); sink(S::K);
  sink(tmpl(x)); sink(tmpl<U32>(3));
  sink('a'); sink(sizeof(U32)); bool b = true; (void)b;
  U32 arr[7] = {11, 12}; (void)arr;
  U32 arr2[3]; (void)arr2;
  sink(x == 2047 ? 33 : 34);
  for (U32 i = 0; i < 50; i++) { }
  switch (x) { case 60: break; case 61: break; default: break; }
  sink(+5); sink(-6); sink(-(7)); sink((U32)-8);
  sink(1u << 9); sink(x & 0x0F); sink(x % 60);
  static_assert(sizeof(U32) == 4, "");
  (void)lc; (void)lce; (void)ln; (void)ls; (void)lsc;
}
