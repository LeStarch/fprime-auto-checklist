typedef float F32;
typedef double F64;
typedef long double F128;
void takeF(F32);
F32 retF(F64 d) { return d; }
F32 ret2(F64 d) { return static_cast<F32>(d); }
struct S { F32 f; S(F64 d) : f(d) {} };
void cases(F64 d, F32 f, F128 ld, int i, F64 (&arr)[2]) {
  F32 a = d;
  F32 b = (F32)d;
  F32 c = 1.5;
  F32 e = 1.5 + 0.5;
  const F64 k = 2.0;
  F32 g = k;
  F32 h = d * 2.0;
  F32 j = f;
  F32 l = ld;
  F64 m = ld;
  F32 n = i;
  F64 o = f;
  takeF(d);
  takeF(f * d);
  takeF(arr[0]);
  f = d;
  f += d;
  f = f + d;
  F32 p = d > 0 ? d : f;
  F32 q = static_cast<F32>(d) * d;
  F32 r = -d;
  F32 s[2] = {d, 1.0};
  F32 t = {d};
  (void)a; (void)b; (void)c; (void)e; (void)g; (void)h; (void)j; (void)l; (void)m; (void)n; (void)o; (void)p; (void)q; (void)r; (void)s; (void)t;
  auto lam = [](F64 x) { F32 y = x; return y; };
  lam(1.0);
  F32 u = retF(d) + d;
  (void)u;
  F32 v = sizeof(F64) * 1.5;
  (void)v;
}
template <typename T> F32 tmpl(T t) { F32 x = t; return x; }
void inst() { tmpl(1.0); tmpl(1.0f); }
