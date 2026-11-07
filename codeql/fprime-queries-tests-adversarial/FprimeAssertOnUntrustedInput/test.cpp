typedef unsigned int U32;
typedef unsigned int FwOpcodeType;
namespace Fw { void SwAssert(int); struct ParamValid { enum T { VALID, INVALID }; }; }
#define FW_ASSERT(cond, ...) ((cond) ? static_cast<void>(0) : Fw::SwAssert(__LINE__))
struct Comp {
  U32 m_v;
  U32 paramGet_LIMIT(Fw::ParamValid::T& valid);
  U32 paramGet_OTHER(Fw::ParamValid::T& valid);
  void check(U32 v) { FW_ASSERT(v < 10); }
  void A_cmdHandler(FwOpcodeType op, U32 seq, U32 arg) { FW_ASSERT(arg < 10); }
  void B_cmdHandler(FwOpcodeType op, U32 seq, U32 arg) { FW_ASSERT(op != 0); FW_ASSERT(seq != 0); }
  void C_cmdHandler(FwOpcodeType op, U32 seq, U32 arg) { U32 copy = arg; FW_ASSERT(copy < 10); }
  void D_cmdHandler(FwOpcodeType op, U32 seq, U32 arg) { U32 derived = arg + 1; FW_ASSERT(derived < 10); }
  void E_cmdHandler(FwOpcodeType op, U32 seq, U32 arg) { check(arg); }
  void F_cmdHandler(FwOpcodeType op, U32 seq, U32 arg) { m_v = arg; FW_ASSERT(m_v < 10); }
  void G_cmdHandler(FwOpcodeType op, U32 seq, U32 arg) { if (arg >= 10) { return; } FW_ASSERT(arg < 10); }
  void H_cmdHandler(FwOpcodeType op, U32 seq, U32 arg, U32 arg2) { FW_ASSERT(arg2 < 10, arg); }
  void I_cmdHandler(FwOpcodeType op, U32 seq, const U32& arg) { FW_ASSERT(arg < 10); }
  void J_cmdHandler(FwOpcodeType op, U32 seq, U32 arg) { auto l = [arg] { FW_ASSERT(arg < 10); }; l(); }
  void K_cmdHandler(FwOpcodeType op, U32 seq, U32 arg) { U32 x = 0; x = arg; FW_ASSERT(x < 10); }
  void L_cmdHandler(FwOpcodeType op, U32 seq, U32 arg) { FW_ASSERT(arg * 2 < 10); }
  void M_cmdHandler(FwOpcodeType op, U32 seq, U32 arg) { U32 a = arg; U32 b = a; U32 c = b; FW_ASSERT(c < 10); }
  void param() { Fw::ParamValid::T v; U32 lim = paramGet_LIMIT(v); FW_ASSERT(lim < 10); }
  void paramDirect() { Fw::ParamValid::T v; FW_ASSERT(paramGet_LIMIT(v) < 10); }
  void paramValid() { Fw::ParamValid::T v; U32 lim = paramGet_LIMIT(v); FW_ASSERT(v == Fw::ParamValid::VALID); (void)lim; }
  void paramDerived() { Fw::ParamValid::T v; U32 lim = paramGet_OTHER(v) + 1; FW_ASSERT(lim < 10); }
  void paramMember() { Fw::ParamValid::T v; m_v = paramGet_LIMIT(v); FW_ASSERT(m_v < 10); }
  void notHandler(U32 arg) { FW_ASSERT(arg < 10); }
  void x_internalInterfaceHandler(U32 arg) { FW_ASSERT(arg < 10); }
  void N_cmdHandler(FwOpcodeType op, U32 seq, U32 arg) { U32* p = &arg; FW_ASSERT(*p < 10); }
  void O_cmdHandler(FwOpcodeType op, U32 seq, U32 arg) { arg = 5; FW_ASSERT(arg < 10); }
  void P_cmdHandler(FwOpcodeType op, U32 seq, U32 arg) { if (seq) { arg = 5; } FW_ASSERT(arg < 10); }
  void Q_cmdHandler(FwOpcodeType op, U32 seq, U32 arg) { if (seq) { arg = 5; } else { arg = 6; } FW_ASSERT(arg < 10); }
  void R_cmdHandler(FwOpcodeType op, U32 seq, U32 arg) { for (U32 i = 0; i < seq; i++) { FW_ASSERT(arg < 10); arg = 5; } }
};
