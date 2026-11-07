namespace Fw { void SwAssert(int line); namespace StringUtils { unsigned string_length(const char*, unsigned); } }
#define FW_ASSERT(cond) ((cond) ? static_cast<void>(0) : Fw::SwAssert(__LINE__))
#define assert(cond) ((cond) ? static_cast<void>(0) : Fw::SwAssert(__LINE__))
#define ASSERT(cond) ((cond) ? static_cast<void>(0) : Fw::SwAssert(__LINE__))
int* __errno_location();
#define errno (*__errno_location())
typedef unsigned int U32;
extern "C" unsigned long strlen(const char*);
U32 g;
U32 pure(U32 x) { return x + 1; }
U32 impure() { g++; return g; }
U32 undefinedFn();
U32 localWrite(U32 x) { U32 t = x; t++; t += 2; return t; }
U32 callsImpure() { return impure(); }
U32 chain1() { return impure(); }
U32 chain2() { return chain1(); }
U32 chain3() { return chain2(); }
U32 chain4() { return chain3(); }
U32 chain5() { return chain4(); }
U32 chain6() { return chain5(); }
U32 outParam(U32* out) { *out = 1; return 1; }
U32 refParam(U32& out) { out = 1; return 1; }
struct Obj {
  U32 v;
  U32 constGet() const { return v; }
  U32 getter() { return v; }
  U32 getter() const { return v; }
  U32 nonConstPure() { return v; }
  U32 nonConstImpure() { v++; return v; }
  virtual U32 virtConst() const { return v; }
  virtual U32 virtNonConst() { return v; }
  U32 memberWrite() { return v = 2; }
  static U32 staticPure(U32 x) { return x; }
  U32 operator()() const { return v; }
  bool operator==(const Obj& o) const { return v == o.v; }
  Obj& operator=(const Obj& o) { v = o.v; return *this; }
};
void cases(U32 x, Obj o, Obj* po, const char* s, U32 arr[4]) {
  FW_ASSERT(x++ < 5);
  FW_ASSERT((x = 5) == 5);
  FW_ASSERT(pure(x) > 0);
  FW_ASSERT(impure() > 0);
  FW_ASSERT(undefinedFn() > 0);
  FW_ASSERT(localWrite(x) > 0);
  FW_ASSERT(callsImpure() > 0);
  FW_ASSERT(chain6() > 0);
  FW_ASSERT(outParam(&x) > 0);
  FW_ASSERT(refParam(x) > 0);
  FW_ASSERT(o.constGet() > 0);
  FW_ASSERT(o.getter() > 0);
  FW_ASSERT(o.nonConstPure() > 0);
  FW_ASSERT(o.nonConstImpure() > 0);
  FW_ASSERT(o.virtConst() > 0);
  FW_ASSERT(o.virtNonConst() > 0);
  FW_ASSERT(po->virtNonConst() > 0);
  FW_ASSERT(o.memberWrite() > 0);
  FW_ASSERT(Obj::staticPure(x) > 0);
  FW_ASSERT(o() > 0);
  FW_ASSERT(o == o);
  FW_ASSERT((o = o).v > 0);
  FW_ASSERT(strlen(s) > 0);
  FW_ASSERT(errno == 0);
  FW_ASSERT(Fw::StringUtils::string_length(s, 10) > 0);
  FW_ASSERT(pure(x = 1) > 0);
  auto lam = [](U32 a) { return a; };
  FW_ASSERT(lam(x) > 0);
  auto mut = [&x]() { return x++; };
  FW_ASSERT(mut() > 0);
  FW_ASSERT(arr[x++] > 0);
  FW_ASSERT(--x > 0);
  FW_ASSERT(x += 1);
  FW_ASSERT(new U32 != nullptr);
  assert(x++ > 0);
  ASSERT(x++ > 0);
  FW_ASSERT(sizeof(x++) > 0);
  FW_ASSERT(x > 0 || impure() > 0);
  FW_ASSERT((x > 0, x < 5));
}
template <typename T> void tmpl(T v) { FW_ASSERT(v++ > 0); FW_ASSERT(pure(v) > 0); }
void inst() { tmpl(1u); }
U32 pchain1(U32 x) { return pure(x); }
U32 pchain2(U32 x) { return pchain1(x); }
U32 pchain3(U32 x) { return pchain2(x); }
U32 pchain4(U32 x) { return pchain3(x); }
U32 pchain5(U32 x) { return pchain4(x); }
U32 pchain6(U32 x) { return pchain5(x); }
void deepPure(U32 x) { FW_ASSERT(pchain6(x) > 0); }
