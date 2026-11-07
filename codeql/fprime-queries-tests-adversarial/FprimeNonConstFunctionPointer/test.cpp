typedef unsigned int U32;
typedef void (*Fn)(U32);
typedef void (*const CFn)(U32);
typedef void (Comp_dummy)(U32);
void target(U32);
struct Obj { void method(U32); static void sm(U32); };
typedef void (Obj::*MFn)(U32);
struct Table { Fn fn; CFn cfn; MFn mfn; };
Fn g_fn = target;
CFn g_cfn = target;
const Fn g_cfn2 = target;
Fn const g_cfn3 = target;
Fn g_table[2] = {target, target};
const Fn g_ctable[2] = {target, target};
void cases(Fn p, CFn cp, const Fn cp2, Fn& rp, const Fn& crp, Table t, Table* pt, Obj o, MFn m, Fn (&arrref)[2]) {
  p(1);
  cp(1);
  cp2(1);
  rp(1);
  crp(1);
  (*p)(1);
  (*cp)(1);
  g_fn(1);
  g_cfn(1);
  g_cfn2(1);
  g_cfn3(1);
  g_table[0](1);
  g_ctable[0](1);
  t.fn(1);
  t.cfn(1);
  pt->fn(1);
  pt->cfn(1);
  (o.*m)(1);
  (o.*t.mfn)(1);
  target(1);
  Obj::sm(1);
  o.method(1);
  arrref[0](1);
  auto l = [](U32 v) { (void)v; };
  l(1);
  Fn local = target;
  local(1);
  const Fn clocal = target;
  clocal(1);
  Fn* pp = &local;
  (*pp)(1);
  (**pp)(1);
  p ? p(1) : cp(1);
}
template <typename F> void tmpl(F f) { f(1); }
void inst() { tmpl(g_fn); tmpl(g_cfn); }
void constPtrToPtr(Fn* const cpp) { (**cpp)(1); }
void constPtrToConstPtr(const Fn* const cpp) { (**cpp)(1); }
