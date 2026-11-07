struct Foo { int v; };
int plain;
static int fileStatic;
extern int externOnly;
extern int externDefined;
int externDefined = 1;
const int constant = 1;
constexpr int cexpr = 2;
const char* ptrToConst = "x";
const char* const constPtr = "y";
const int constArr[2] = {1, 2};
int mutArr[2];
volatile int vol;
namespace { int anon; const int anonConst = 1; }
namespace ns { int inNs; static const int nsConst = 3; }
Foo fooGlobal;
const Foo constFoo = {1};
template <typename T> T zero = T();
struct Holder { static int sm; static const int scm; };
int Holder::sm = 0;
void fn() {
  static int counter;
  static const int limit = 10;
  static Foo foo;
  static constexpr int ce = 1;
  thread_local int tl = 0;
  int local = 0;
  auto l = [] { static int inLambda; return inLambda++; };
  (void)counter; (void)limit; (void)foo; (void)ce; (void)tl; (void)local; l();
}
template <typename T> void tmpl() { static T cache; (void)cache; }
void inst() { tmpl<int>(); tmpl<float>(); }
int& ref = plain;
const int& cref = plain;
