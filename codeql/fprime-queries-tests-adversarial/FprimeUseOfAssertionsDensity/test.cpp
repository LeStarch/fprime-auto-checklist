namespace Fw { void SwAssert(int line); }
#define FW_ASSERT(cond) ((cond) ? static_cast<void>(0) : Fw::SwAssert(__LINE__))
#define CHECK(cond) FW_ASSERT(cond)
#define ASSERT_EQ(a, b) ((a) == (b) ? static_cast<void>(0) : Fw::SwAssert(__LINE__))
typedef unsigned int U32;
typedef bool Bool;
struct Status { enum T { OK, FAIL }; };
void log_WARNING_HI_Bad(U32);
void log_ACTIVITY_LO_Fine(U32);
void sink(U32);
void noAssert(U32 x) {
  sink(1);
  sink(2);
  sink(3);
  sink(4);
  sink(5);
  sink(6);
  sink(7);
  sink(8);
  sink(9);
  sink(10);
}
void exactlyTen(U32 x) {
  sink(1);
  sink(2);
  sink(3);
  sink(4);
  sink(5);
  sink(6);
  sink(7);
  sink(8);
}
void viaMacro(U32 x) {
  CHECK(x > 0);
  sink(1);
  sink(2);
  sink(3);
  sink(4);
  sink(5);
  sink(6);
  sink(7);
  sink(8);
  sink(9);
}
void commentedOut(U32 x) {
  // FW_ASSERT(x > 0);
  sink(1);
  sink(2);
  sink(3);
  sink(4);
  sink(5);
  sink(6);
  sink(7);
  sink(8);
  sink(9);
}
void inString(U32 x) {
  const char* s = "FW_ASSERT(x)";
  sink(1);
  sink(2);
  sink(3);
  sink(4);
  sink(5);
  sink(6);
  sink(7);
  sink(8);
  (void)s;
}
void assertInLambda(U32 x) {
  auto l = [x] { FW_ASSERT(x > 0); };
  l();
  sink(1);
  sink(2);
  sink(3);
  sink(4);
  sink(5);
  sink(6);
  sink(7);
  sink(8);
}
void longLambdaHost(U32 x) {
  auto l = [x] {
    sink(1);
    sink(2);
    sink(3);
    sink(4);
    sink(5);
    sink(6);
    sink(7);
    sink(8);
    sink(9);
    sink(10);
  };
  l();
}
Bool returnsBoolTypedef(U32 x) {
  sink(1);
  sink(2);
  sink(3);
  sink(4);
  sink(5);
  sink(6);
  sink(7);
  sink(8);
  sink(9);
  return true;
}
Status::T returnsStatus(U32 x) {
  sink(1);
  sink(2);
  sink(3);
  sink(4);
  sink(5);
  sink(6);
  sink(7);
  sink(8);
  sink(9);
  return Status::OK;
}
Status returnsStatusClass(U32 x) {
  sink(1);
  sink(2);
  sink(3);
  sink(4);
  sink(5);
  sink(6);
  sink(7);
  sink(8);
  sink(9);
  return Status();
}
void logsLow(U32 x) {
  log_ACTIVITY_LO_Fine(x);
  sink(1);
  sink(2);
  sink(3);
  sink(4);
  sink(5);
  sink(6);
  sink(7);
  sink(8);
  sink(9);
}
void logsHigh(U32 x) {
  log_WARNING_HI_Bad(x);
  sink(1);
  sink(2);
  sink(3);
  sink(4);
  sink(5);
  sink(6);
  sink(7);
  sink(8);
  sink(9);
}
void gtest(U32 x) {
  ASSERT_EQ(x, 1u);
  sink(1);
  sink(2);
  sink(3);
  sink(4);
  sink(5);
  sink(6);
  sink(7);
  sink(8);
  sink(9);
}
void withStaticAssert(U32 x) {
  static_assert(sizeof(U32) == 4, "size");
  sink(1);
  sink(2);
  sink(3);
  sink(4);
  sink(5);
  sink(6);
  sink(7);
  sink(8);
  sink(9);
}
template <typename T> void tmpl(T x) {
  sink(1);
  sink(2);
  sink(3);
  sink(4);
  sink(5);
  sink(6);
  sink(7);
  sink(8);
  sink(9);
  sink(10);
}
void inst() { tmpl(1u); tmpl(2); }
struct C {
  C(U32 x) {
    sink(1);
    sink(2);
    sink(3);
    sink(4);
    sink(5);
    sink(6);
    sink(7);
    sink(8);
    sink(9);
    sink(10);
  }
  ~C() {
    sink(1);
    sink(2);
    sink(3);
    sink(4);
    sink(5);
    sink(6);
    sink(7);
    sink(8);
    sink(9);
    sink(10);
  }
  void method(U32 x) {
    sink(1);
    sink(2);
    sink(3);
    sink(4);
    sink(5);
    sink(6);
    sink(7);
    sink(8);
    sink(9);
    sink(10);
  }
  virtual void virt(U32 x) {
    sink(1); sink(2); sink(3); sink(4); sink(5);
    sink(6); sink(7); sink(8); sink(9); sink(10);
  }
};
void commentsDontCount(U32 x) {
  // one
  // two
  // three
  /* four
     five
     six */
  sink(1);
  sink(2);
  sink(3);
}
void braceLines(U32 x)
{
  if (x)
  {
    sink(1);
  }
  else
  {
    sink(2);
  }
}
