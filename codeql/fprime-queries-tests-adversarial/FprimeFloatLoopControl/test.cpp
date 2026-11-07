struct S { float f; double d; };
typedef float F32;
float getF();
void loops(float x, double y, S s, S* ps, float (&arr)[4], float* p, F32 t, int n) {
  for (float f = 0.0f; f < x; f += 0.1f) {}
  for (int i = 0; i < 4 && arr[i] > 0.0f; ++i) {}
  for (int i = 0; i < n; ++i, x += 1.0f) {}
  while (s.f < 1.0f) { s.f += 0.1f; }
  while (ps->d > 0.0) { ps->d -= 1.0; }
  while (*p > 0.0f) { ++p; }
  while (getF() < 1.0f) {}
  do { t -= 1.0f; } while (t > 0.0f);
  while (x) { x -= 1.0f; }
  while (static_cast<int>(y) < 3) { y += 1.0; }
  for (float v : arr) { (void)v; }
  for (int i = 0; i < n; ++i) { if (x > 1.0f) { break; } }
  while (n-- > 0) {}
}
template <typename T> void tmpl(T lim) { for (T v = 0; v < lim; ++v) {} }
void inst() { tmpl(3); tmpl(2.5f); }
