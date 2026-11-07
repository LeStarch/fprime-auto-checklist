// No system headers: the CodeQL test extractor has no system include path, so the
// external-API exemption is exercised only by the whole-tree comparison, not here.
typedef unsigned long size_t;
typedef int int32_t; typedef unsigned char uint8_t;
size_t strlen(const char*);
int* errno_location();
#define errno (*errno_location())
typedef unsigned int U32;
typedef int I32;
typedef unsigned char U8;
typedef U32 Alias;
typedef unsigned long Bad;
typedef Bad Bad2;
typedef int32_t Good32;
struct Ext;
extern "C" int external_fn(int x, long* out);
int sys_like();
void fine(U32 a, uint8_t b, Alias c, Good32 d, bool e, char* s, const char* cs, char** ss, float f, double g);
void bad(int a, unsigned b, long c, short d, char e, unsigned char f, Bad g, Bad2 h, signed char i, long long j);
void badPtr(int* a, unsigned& b, const long* c, int arr[], int (*fp)(int));
int badRet();
unsigned long badRet2();
U32 goodRet();
char badCharRet();
char* okCharPtrRet();
struct S { int m_bad; U32 m_ok; char m_c; char* m_cs; int method(int p); U32 ok(U32 p); };
int g_bad;
U32 g_ok;
static const int g_cbad = 1;
enum E { X };
void usesExternal(int plainArg) {
  int err = errno;
  int r = external_fn(1, nullptr);
  long out = 0;
  external_fn(1, &out);
  int len = strlen("x");
  int local = 0;
  int cmp = 0;
  if (cmp == external_fn(0, nullptr)) { }
  int viaVar = r;
  int fromParam = plainArg;
  size_t sz = sizeof(int);
  int forLoop = 0;
  for (int i = 0; i < 3; i++) { }
  int noIlist = 0; (void)noIlist; // std::initializer_list unavailable without system headers
  auto a = 1;
  auto b = 1u;
  int sl = sys_like();
  (void)err; (void)len; (void)local; (void)viaVar; (void)fromParam; (void)sz; (void)forLoop; (void)a; (void)b; (void)sl;
}
int passthrough(int x) { return external_fn(x, nullptr); }
int wrapper(int x) { return x; }
void callsWrapper() { int v = 0; wrapper(v); int w = 0; passthrough(w); }
template <typename T> T tmpl(T t) { int inner = 0; (void)inner; return t; }
void inst() { tmpl(1); tmpl(1u); }
struct Ops { Ops operator++(int); Ops& operator--(int unusedName); Ops operator+(int); };
auto lambda = [](int p) { int q = p; return q; };
void lambdaUser() { lambda(1); }
long with_comparison_ext(long v) { if (v > strlen("")) { } return v; }
unsigned int spelled(unsigned int x) { return x; }
int main() { return 0; }
