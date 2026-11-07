typedef unsigned int U32;
namespace Fw { void SwAssert(int); }
#define FW_ASSERT(cond, ...) ((cond) ? static_cast<void>(0) : Fw::SwAssert(__LINE__))
struct Obj { U32 x; U32 get() const; bool valid; };
void sink(U32);
typedef void (*Fn)(void);
struct Comp {
  U32 m_arr[8];
  void deref(Obj* p) { sink(p->x); }
  void star(Obj* p) { sink((*p).x); }
  void index(U32* p) { sink(p[0]); }
  void arith(U32* p) { sink(*(p + 1)); }
  void call(Obj* p) { sink(p->get()); }
  void guarded(Obj* p) { if (p != nullptr) { sink(p->x); } }
  void asserted(Obj* p) { FW_ASSERT(p != nullptr); sink(p->x); }
  void merged(Obj* p, bool c) { if (c) { if (p) { sink(1); } } sink(p->x); }
  void reassigned(Obj* p) { Obj local; p = &local; sink(p->x); }
  void incremented(U32* p, bool c) { if (c) { p++; } sink(*p); }
  void inCondition(Obj* p) { if (p->valid) { sink(p->x); } }
  void derefBeforeCheck(Obj* p) { U32 v = p->x; if (p) { sink(v); } }
  void notGuard(Obj* p) { if (p == nullptr) { sink(0); } else { sink(p->x); } }
  void returnGuard(Obj* p) { if (!p) { return; } sink(p->x); }
  void loopGuard(Obj* p) { while (p) { sink(p->x); p = nullptr; } }
  void lambda(Obj* p) { auto l = [p] { return p->x; }; sink(l()); }
  void indexParam(U32 i) { sink(m_arr[i]); }
  void divParam(U32 d, U32 a) { sink(a / d); sink(a % d); }
  void indexGuarded(U32 i) { if (i < 8) { sink(m_arr[i]); } }
  void indexExpr(U32 i) { sink(m_arr[i + 1]); }
  void refParam(U32& r) { sink(m_arr[r]); }
  void constCharStar(const char* s) { sink(s[0]); }
  void fnPtr(Fn f) { f(); }
  void fnPtrGuarded(Fn f) { if (f != nullptr) { f(); } }
  void compared(Obj* p, Obj* q) { if (p == q) { sink(p->x); sink(q->x); } }
  void twoUses(Obj* p) { FW_ASSERT(p); sink(p->x); sink(p->get()); }
  void ternary(Obj* p) { sink(p ? p->x : 0u); }
  void andGuard(Obj* p) { if (p && p->valid) { sink(p->x); } }
  void orGuard(Obj* p, bool c) { if (c || p->valid) { sink(p->x); } }
  static void staticMember(Obj* p) { sink(p->x); }
  void switchUse(U32 i) { switch (i) { case 1: sink(m_arr[i]); break; default: break; } }
  void doWhile(Obj* p) { do { sink(p->x); } while (p == nullptr); }
  void castDeref(void* v) { sink(static_cast<Obj*>(v)->x); }
  void doubleDeref(Obj** pp) { sink((*pp)->x); }
  void defaultArg(Obj* p = nullptr) { sink(p->x); }
  template <typename T> void tmpl(T* p) { sink(p->x); }
  void inst(Obj* o) { tmpl(o); }
};
void freeFn(Obj* p) { sink(p->x); }
static void staticFree(Obj* p) { sink(p->x); }
namespace { void anonFree(Obj* p) { sink(p->x); } }
