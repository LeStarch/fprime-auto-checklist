typedef unsigned int U32;
U32 f();
U32 g();
void v();
U32& ref();
U32* ptr();
struct Big { U32 a; ~Big(); };
Big makeBig();
struct Obj {
  U32 method();
  virtual U32 virt();
  Obj& operator=(const Obj&);
  Obj& operator++();
  Obj& operator+=(U32);
  U32 operator()();
};
void sink(U32);
typedef U32 (*Fp)();
void cases(Obj o, Obj* po, Fp fp, U32& out, bool c) {
  f();
  (void)f();
  static_cast<void>(g());
  U32 r1 = f();
  U32 r2 = f(); sink(r2);
  U32 r3; r3 = f(); sink(r3);
  U32 r4; r4 = f(); r4 = g(); sink(r4);
  if (f()) { sink(1); }
  f() + 1;
  o.method();
  po->virt();
  o = o;
  ++o;
  o += 1;
  o();
  __builtin_expect(f(), 1);
  v();
  ref();
  ptr();
  makeBig();
  fp();
  for (U32 i = 0; i < 2; f(), i++) { }
  f(), sink(1);
  auto l = [] { f(); };
  l();
  U32 r5 = f(); (void)r5;
  constexpr U32 ce = 4;
  (void)ce;
  out = f();
  static U32 st = f();
  (void)st;
  U32 r6;
  if ((r6 = f()) != 0) { sink(0); }
  U32 r7 = f();
  if (c) { sink(r7); }
  U32 r8 = f();
  r8 = g();
  U32 r9 = c ? f() : g();
  (void)r9;
  U32 r10 = f(), r11 = g();
  sink(r10);
  U32 r12 = f();
  r12++;
  U32 r13 = f();
  U32 r14 = r13;
  (void)r14;
  while (f()) { }
  return f(), (void)0;
}
U32 returned() { return f(); }
template <typename T> void tmpl(T t) { f(); T x = f(); (void)t; }
void inst() { tmpl(1); }
