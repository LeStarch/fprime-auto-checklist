void sink(int);
void loop_isr() { for (int i = 0; i < 3; ++i) { sink(i); } }
void rangefor_isr(int (&arr)[3]) { for (int v : arr) { sink(v); } }
void dowhile_isr() { int i = 0; do { ++i; } while (i < 3); }
void goto_isr() { int i = 0; again: ++i; if (i < 3) goto again; }
void lambda_loop_isr() { auto l = [] { for (int i = 0; i < 3; ++i) {} }; l(); }
struct L { void run() { while (true) {} } };
void localclass_isr() { struct X { void f() { for (;;) {} } }; X x; x.f(); }
void twenty_isr(int x) {
  sink(1); sink(2); sink(3); sink(4); sink(5);
  sink(6); sink(7); sink(8); sink(9); sink(10);
  sink(11); sink(12); sink(13); sink(14); sink(15);
  sink(16); sink(17); sink(18); sink(19); sink(20);
}
void twentyone_isr(int x) {
  sink(1); sink(2); sink(3); sink(4); sink(5);
  sink(6); sink(7); sink(8); sink(9); sink(10);
  sink(11); sink(12); sink(13); sink(14); sink(15);
  sink(16); sink(17); sink(18); sink(19); sink(20);
  sink(21);
}
void ifelse_isr(int x) {
  if (x) { sink(1); sink(2); sink(3); sink(4); sink(5); sink(6); }
  else { sink(7); sink(8); sink(9); sink(10); sink(11); sink(12); }
  if (x > 1) { sink(13); sink(14); sink(15); sink(16); sink(17); sink(18); }
}
void switch_isr(int x) {
  switch (x) {
    case 0: sink(0); sink(1); sink(2); sink(3); break;
    case 1: sink(4); sink(5); sink(6); sink(7); break;
    case 2: sink(8); sink(9); sink(10); sink(11); break;
    default: sink(12); sink(13); break;
  }
}
void decl_isr() {
  int a = 1; int b = 2; int c = 3; int d = 4; int e = 5; int f = 6; int g = 7;
  int h = 8; int i = 9; int j = 10; int k = 11; int l = 12; int m = 13; int n = 14;
  int o = 15; int p = 16; int q = 17; int r = 18; int s = 19; int t = 20; int u = 21;
  (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)g; (void)h; (void)i; (void)j; (void)k;
  (void)l; (void)m; (void)n; (void)o; (void)p; (void)q; (void)r; (void)s; (void)t; (void)u;
}
void empty_isr() {}
void declared_isr();
