typedef unsigned int U32;
U32 g_count;
bool pure(U32 x) { return x > 1; }
bool impureGlobal(U32 x) { g_count = x; return true; }
bool impureStatic() { static U32 s = 0; s++; return s > 1; }
bool callsImpure(U32 x) { return impureGlobal(x); }
bool constPtr(const U32* p) { return *p > 1; }
bool nonConstPtr(U32* p) { return *p > 1; }
bool nonConstRef(U32& r) { return r > 1; }
bool constRef(const U32& r) { return r > 1; }
bool undefined(U32 x);
constexpr bool cx(U32 x) { return x > 1; }
struct S {
  U32 m;
  bool constMethod() const { return m > 1; }
  bool nonConstMethod() { return m > 1; }
  bool mutating() { m++; return true; }
  static bool sm(U32 x) { return x > 1; }
  virtual bool virt() const { return true; }
  bool localOnly(U32 x) const { U32 t = x; t++; return t > 1; }
  bool callsConst() const { return constMethod(); }
};
struct Ptrs { U32* p; };
bool structWithPtr(Ptrs s) { return s.p != nullptr; }
bool deletes(U32* p) { delete p; return true; }
bool news() { U32* p = new U32; (void)p; return true; }
typedef bool (*Pred)(U32);
template <typename T> bool tmplPure(T t) { return t > 1; }
template <typename T> bool tmplImpure(T t) { g_count = t; return true; }
void cases(U32 x, S s, S* sp, Pred fp, U32* p, U32& r, U32 (&arr)[4]) {
  if (x > 1) { }
  if (x++) { }
  if (x = 1) { }
  if (pure(x)) { }
  if (impureGlobal(x)) { }
  if (impureStatic()) { }
  if (callsImpure(x)) { }
  if (constPtr(p)) { }
  if (nonConstPtr(p)) { }
  if (nonConstRef(r)) { }
  if (constRef(r)) { }
  if (undefined(x)) { }
  if (cx(x)) { }
  if (s.constMethod()) { }
  if (s.nonConstMethod()) { }
  if (s.mutating()) { }
  if (S::sm(x)) { }
  if (s.virt()) { }
  if (sp->virt()) { }
  if (s.localOnly(x)) { }
  if (s.callsConst()) { }
  if (structWithPtr(Ptrs())) { }
  if (deletes(p)) { }
  if (news()) { }
  if (fp(x)) { }
  if (tmplPure(x)) { }
  if (tmplImpure(x)) { }
  while (x-- > 0) { }
  while (pure(x)) { }
  while (impureGlobal(x)) { }
  for (U32 i = 0; i < x; i++) { }
  for (U32 i = 0; impureGlobal(i); i++) { }
  do { } while (x--);
  do { } while (pure(x));
  U32 y = x > 1 ? 1 : 2; (void)y;
  U32 z = impureGlobal(x) ? 1 : 2; (void)z;
  for (U32 v : arr) { (void)v; }
  if (pure(x) && impureGlobal(x)) { }
  if (pure(x) || x++) { }
  if (!impureGlobal(x)) { }
  if ((x = 2)) { }
  if (x += 1) { }
  if (static_cast<bool>(impureGlobal(x))) { }
  if (sizeof(impureGlobal(x))) { }
  bool b = impureGlobal(x); if (b) { }
  if (arr[x++]) { }
  if (p && *p) { }
  switch (x++) { default: break; }
  if (auto q = impureGlobal(x)) { }
  if (x > 1) { } else if (impureGlobal(x)) { }
}
