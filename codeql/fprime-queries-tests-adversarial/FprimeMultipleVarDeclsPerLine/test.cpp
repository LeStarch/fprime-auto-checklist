#define DECL_PAIR(a, b) int a, b;
#define DECL_ONE(a) int a;
struct S { int x; };
void f(int arr[]) {
  int a, b;
  int c,
      d;
  int e; int g;
  for (int i = 0, j = 0; i < 3; ++i, ++j) {}
  static int s1, s2;
  int *p, q;
  struct T { int y; } t1, t2;
  DECL_PAIR(m1, m2)
  DECL_ONE(m3) DECL_ONE(m4)
  int (*fp1)(int), (*fp2)(int);
  int arr2[2], arr3[3];
  int solo = 1;
  (void)a; (void)b; (void)c; (void)d; (void)e; (void)g; (void)s1; (void)s2; (void)p; (void)q;
  (void)t1; (void)t2; (void)m1; (void)m2; (void)m3; (void)m4; (void)fp1; (void)fp2; (void)arr2; (void)arr3; (void)solo;
}
int g1, g2;
S s1, s2;
template <typename T> void tmpl() { T t1, t2; (void)t1; (void)t2; }
void inst() { tmpl<int>(); }
