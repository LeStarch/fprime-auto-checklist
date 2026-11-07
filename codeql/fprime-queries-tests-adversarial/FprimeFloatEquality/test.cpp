struct P { float f; double d; };
typedef float F32;
#define FLT_EQ(a, b) ((a) == (b))
bool cases(float a, float b, double d, int i, P p, P* pp, F32 arr[2], F32 t) {
  bool r = false;
  r |= a == b;
  r |= a != a;
  r |= p.f == p.f;
  r |= pp->d != pp->d;
  r |= arr[0] == arr[0];
  r |= a == 0;
  r |= i == 1.0f;
  r |= (int)a == i;
  r |= static_cast<int>(d) != i;
  r |= FLT_EQ(a, b);
  r |= t == 1.0f;
  r |= (a + b) == d;
  r |= &a == &b;
  r |= !(a == b);
  r |= (a == b) == (d == 1.0);
  return r;
}
template <typename T> bool tmpl(T x, T y) { return x == y; }
bool inst() { return tmpl(1, 2) || tmpl(1.0f, 2.0f) || tmpl(1.0, 2.0); }
struct Cmp { bool operator==(const Cmp&) const; float v; };
bool overloaded(Cmp a, Cmp b) { return a == b; }
