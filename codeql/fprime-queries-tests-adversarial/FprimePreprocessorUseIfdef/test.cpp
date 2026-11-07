#define FW_FOO 1
#define LOCAL_SWITCH 1
void sink(int);
void cases() {
#ifdef LOCAL_SWITCH
  sink(1);
#endif
#ifndef LOCAL_SWITCH
  sink(2);
#else
  sink(3);
#endif
#if LOCAL_SWITCH
  sink(4);
#elif defined(FW_FOO)
  sink(5);
#endif
#if defined(FW_FOO) && FW_FOO
  sink(6);
#endif
#if 0
  sink(7);
#endif
#if 1
  sink(8);
#endif
#if __cplusplus >= 201103L
  sink(9);
#endif
#ifdef __linux__
  sink(10);
#endif
#ifdef BUILD_UT
  sink(11);
#endif
#if defined(FW_FOO) || defined(BUILD_UT)
  sink(12);
#endif
#ifdef UNKNOWN_EXTERNAL
  sink(13);
#endif
# ifdef LOCAL_SWITCH
  sink(14);
# endif
#ifdef LOCAL_SWITCH
# ifdef FW_FOO
  sink(15);
# endif
#endif
}
