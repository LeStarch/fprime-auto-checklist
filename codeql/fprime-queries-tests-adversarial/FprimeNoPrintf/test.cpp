extern "C" int printf(const char*, ...);
extern "C" int puts(const char*);
namespace std { using ::printf; }
#define LOG(...) printf(__VA_ARGS__)
#define PUTS puts
typedef int (*PrintFn)(const char*, ...);
struct Logger { int printf(const char* f, ...) { return 0; } };

void direct() { printf("x"); ::printf("y"); std::printf("z"); }
void viaMacro() { LOG("a"); PUTS("b"); }
void viaPointer() { PrintFn fn = printf; fn("c"); PrintFn fp = &printf; (*fp)("d"); }
void member() { Logger l; l.printf("e"); }
template <typename T> void tmpl(T t) { printf("%d", t); }
void instantiate() { tmpl(1); tmpl(2u); }
void lambda() { auto l = [] { puts("f"); }; l(); }
void addressOnly() { void* p = (void*)&printf; (void)p; }
