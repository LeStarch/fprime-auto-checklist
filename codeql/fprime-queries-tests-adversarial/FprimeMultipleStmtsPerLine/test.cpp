typedef unsigned int U32;
void sink(U32);
U32 f(U32 x) {
  U32 a = 0; U32 b = 1;
  a = 1; b = 2;
  a = 1, b = 2;
  if (x) { a = 1; } else { b = 2; }
  if (x) a = 1; else b = 2;
  if (x) a = 1;
  for (U32 i = 0; i < x; i++) a++;
  for (U32 i = 0; i < x; i++) { a++; b++; }
  while (x--) a++;
  while (x) { x--; a++; }
  do { a++; } while (0);
  do a++; while (0);
  switch (x) { case 1: a = 1; break; case 2: b = 2; break; default: break; }
  switch (x) { case 1: a = 1;
    break; default: break; }
  a++; /* comment */ b++;
  { a++; } { b++; }
  { a++; b++; }
  a = 1; {
    b = 2; }
  return a + b; }
void g() {}
void h() { sink(1); }
void k() { sink(1); sink(2); }
void m() { sink(1);
  sink(2); }
#define TWO(x) sink(x); sink(x)
void n() { TWO(1); }
void o() { sink(1); TWO(2); }
void p() { auto l = [] { sink(1); sink(2); }; l(); }
void q() { if (true) { sink(1); } sink(2); }
void r() { if (true) sink(1); sink(2); }
void s() { for (;;) { break; } sink(1); }
void t() { U32 x = 0; U32 y = 0; (void)x; (void)y; }
void u() { U32 x = 0, y = 0; (void)x; (void)y; }
void v() { label: sink(1); sink(2); goto end; end: ; }
void w() { ; sink(1); }
void x() { sink(1); ; }
void y() { return; }
void z() { sink(1); return; }
