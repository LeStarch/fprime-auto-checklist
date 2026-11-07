typedef unsigned int U32;
namespace Fw { void SwAssert(int); }
#define FW_ASSERT(cond, ...) ((cond) ? static_cast<void>(0) : Fw::SwAssert(__LINE__))
enum { SLOT = 2 };
struct Obj { U32 x; };
struct Holder {
  U32 m_arr[8];
  U32 m_idx;
  U32 member() {
    U32 r = m_arr[m_idx];
    if (m_idx < 8) { r += m_arr[m_idx]; }
    return r;
  }
};
U32 cases(U32 param, U32* p, bool c, U32 (&ref)[4]) {
  U32 arr[8];
  U32 grid[2][2];
  Obj objs[3];
  U32 i = param;
  U32 r = arr[i];
  r += arr[param];
  r += p[i];
  r += ref[i];
  if (i < 8) { r += arr[i]; }
  if (c) { if (i < 8) { r += arr[i]; } }
  r += arr[i];
  FW_ASSERT(i < 8);
  r += arr[i];
  U32 j = param;
  r += arr[j + 1];
  r += arr[(U32)j];
  r += arr[static_cast<unsigned char>(j)];
  r += arr[SLOT];
  const U32 k = 3;
  r += arr[k];
  for (U32 n = 0; n < 8; n++) { r += arr[n]; }
  U32 m = 0;
  while (true) { r += arr[m]; if (m < 7) { break; } }
  U32 a = param, b = param;
  r += grid[a][b];
  r += objs[a].x;
  U32& alias = a;
  if (a < 3) { r += objs[alias].x; }
  U32 d = param;
  r += arr[d--];
  U32 e = param;
  r += arr[e];
  if (e < 8) { r += arr[e]; }
  r += arr[e];
  U32 g = param;
  switch (g) { case 1: r += arr[g]; break; default: break; }
  U32 h = param;
  r += (h < 8) ? arr[h] : 0;
  auto l = [&arr, h] { return arr[h]; };
  r += l();
  return r;
}
template <typename T, unsigned N> T tmpl(T (&a)[N], U32 idx) { return a[idx]; }
U32 inst(U32 (&a)[4]) { U32 i = 1; return tmpl(a, i); }
U32 afterCheck(U32 param) {
  U32 arr[4];
  U32 i = param;
  U32 r = arr[i];
  if (i < 4) { r += arr[i]; }
  return r;
}
