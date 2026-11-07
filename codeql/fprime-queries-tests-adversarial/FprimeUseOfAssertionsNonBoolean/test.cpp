namespace Fw { void SwAssert(int line); }
#define FW_ASSERT(cond, ...) ((cond) ? static_cast<void>(0) : Fw::SwAssert(__LINE__))
#define assert(cond) ((cond) ? static_cast<void>(0) : Fw::SwAssert(__LINE__))
#define ASSERT(cond) ((cond) ? static_cast<void>(0) : Fw::SwAssert(__LINE__))
#define FLAG 0x4u
typedef unsigned int U32;
typedef bool Bool;
enum Mode { MODE_A };
struct Truthy { operator bool() const; };
struct Status { enum T { OK }; };
bool boolFn();
void cases(U32 n, int* p, bool flag, Truthy t, Mode m, Bool b, const bool cb, volatile bool vb, bool& rb, const bool& crb, Status::T st) {
  FW_ASSERT(p);
  FW_ASSERT(n);
  FW_ASSERT(n != 0);
  FW_ASSERT(flag);
  FW_ASSERT(t);
  FW_ASSERT(!n);
  FW_ASSERT(n && p);
  FW_ASSERT(m == MODE_A);
  FW_ASSERT(n = 5);
  FW_ASSERT(n & FLAG);
  FW_ASSERT(static_cast<bool>(n));
  FW_ASSERT(n ? true : false);
  FW_ASSERT((bool)p);
  FW_ASSERT(boolFn());
  FW_ASSERT(m);
  FW_ASSERT(b);
  FW_ASSERT(cb);
  FW_ASSERT(vb);
  FW_ASSERT(rb);
  FW_ASSERT(crb);
  FW_ASSERT(st);
  FW_ASSERT(st == Status::OK);
  FW_ASSERT((n));
  FW_ASSERT(true);
  FW_ASSERT(1);
  FW_ASSERT(nullptr != p);
  FW_ASSERT(flag, n);
  assert(n);
  ASSERT(n);
  FW_ASSERT(n > 0 ? n : 0);
  FW_ASSERT(flag ? n : 0u);
  FW_ASSERT(-flag);
  FW_ASSERT(flag + 0);
  FW_ASSERT(!!n);
}
template <typename T> void tmpl(T v) { FW_ASSERT(v); }
void inst(int i, bool bb) { tmpl(i); tmpl(bb); }
template <typename T> void tmplBool(T v) { FW_ASSERT(v); }
void instBool(bool bb) { tmplBool(bb); }
