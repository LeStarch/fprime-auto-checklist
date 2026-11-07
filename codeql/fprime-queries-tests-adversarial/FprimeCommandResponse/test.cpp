typedef unsigned int U32;
typedef unsigned int FwOpcodeType;
namespace Fw { struct CmdResponse { enum T { OK, EXECUTION_ERROR, VALIDATION_ERROR }; }; }
void sink(U32);
struct Comp {
  FwOpcodeType m_op; U32 m_seq; FwOpcodeType m_op2; U32 m_seq2; U32 m_other; bool m_busy;
  void cmdResponse_out(FwOpcodeType op, U32 seq, Fw::CmdResponse::T r);
  void respondOk(FwOpcodeType op, U32 seq) { cmdResponse_out(op, seq, Fw::CmdResponse::OK); }
  void respondSwapped(U32 seq, FwOpcodeType op) { cmdResponse_out(op, seq, Fw::CmdResponse::OK); }
  void respondNested(FwOpcodeType op, U32 seq) { respondOk(op, seq); }
  void respondWrong(FwOpcodeType op, U32 seq) { cmdResponse_out(op, 0, Fw::CmdResponse::OK); }
  void respondMember() { cmdResponse_out(m_op, m_seq, Fw::CmdResponse::OK); }
  void respondMember2() { cmdResponse_out(m_op2, m_seq2, Fw::CmdResponse::OK); }
  void store(FwOpcodeType op, U32 seq) { m_op2 = op; m_seq2 = seq; }
  void A_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) { cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK); }
  void B_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) { sink(1); }
  void C_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 arg) { if (arg) { cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK); } }
  void D_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 arg) { if (arg) { cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK); return; } cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::EXECUTION_ERROR); }
  void E_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) { respondOk(opCode, cmdSeq); }
  void F_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) { respondSwapped(cmdSeq, opCode); }
  void G_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) { respondNested(opCode, cmdSeq); }
  void H_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) { respondWrong(opCode, cmdSeq); }
  void I_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) { m_op = opCode; m_seq = cmdSeq; }
  void J_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) { m_op = opCode; }
  void K_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) { m_op = opCode; m_other = cmdSeq; }
  void L_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) { store(opCode, cmdSeq); }
  void M_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 arg) { if (arg) { m_op = opCode; m_seq = cmdSeq; } else { cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK); } }
  void N_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 arg) { switch (arg) { case 0: cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK); break; case 1: cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK); break; } }
  void O_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 arg) { switch (arg) { case 0: cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK); break; default: cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK); break; } }
  void P_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 arg) { for (U32 i = 0; i < arg; i++) { cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK); } }
  void Q_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 arg) { while (true) { cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK); break; } }
  void R_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) { auto l = [this, opCode, cmdSeq] { cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK); }; l(); }
  void S_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) { if (m_busy) { return; } cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK); }
  void T_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) { m_busy ? cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK) : sink(1); }
  void U_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) { cmdResponse_out(0, cmdSeq, Fw::CmdResponse::OK); }
  void V_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) { FwOpcodeType o = opCode; U32 s = cmdSeq; cmdResponse_out(o, s, Fw::CmdResponse::OK); }
  void W_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) { this->m_op = opCode; this->m_seq = cmdSeq; }
  void X_cmdHandler(U32 opCode, U32 cmdSeq) { sink(1); }
  void notHandler(FwOpcodeType opCode, U32 cmdSeq) { sink(1); }
  void Y_cmdHandler(FwOpcodeType opCode, U32 cmdSeq);
  void Z_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) { do { cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK); } while (0); }
  void AA_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) { m_seq = cmdSeq; m_op = opCode; }
  void AB_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) { m_op = opCode; m_seq = cmdSeq; cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK); }
  void AC_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, bool arg) { if (arg) { m_op = opCode; } m_seq = cmdSeq; }
  void AD_cmdHandler(const FwOpcodeType opCode, const U32 cmdSeq) { sink(1); }
};
