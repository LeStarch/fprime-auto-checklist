typedef unsigned int U32;
typedef unsigned int FwOpcodeType;
namespace Fw { struct CmdResponse { enum T { OK, INVALID_OPCODE, VALIDATION_ERROR, EXECUTION_ERROR }; CmdResponse(T t) : v(t) {} T v; }; }
void sink(U32);
struct Comp {
  void cmdResponse_out(FwOpcodeType op, U32 seq, Fw::CmdResponse r);
  void A_cmdHandler(FwOpcodeType op, U32 seq, U32 arg) {
    if (arg > 10) {
      this->cmdResponse_out(op, seq, Fw::CmdResponse::VALIDATION_ERROR);
    }
    sink(arg);
  }
  void B_cmdHandler(FwOpcodeType op, U32 seq, U32 arg) {
    if (arg > 10) cmdResponse_out(op, seq, Fw::CmdResponse::VALIDATION_ERROR);
    sink(arg);
  }
  void C_cmdHandler(FwOpcodeType op, U32 seq, U32 arg) {
    if (arg > 10) {
      cmdResponse_out(op, seq, Fw::CmdResponse::VALIDATION_ERROR);
      return;
    }
    sink(arg);
  }
  void D_cmdHandler(FwOpcodeType op, U32 seq, U32 arg) {
    if (arg > 10) {
      cmdResponse_out(op, seq, Fw::CmdResponse::VALIDATION_ERROR);
    } else {
      sink(1);
    }
    sink(arg);
  }
  void E_cmdHandler(FwOpcodeType op, U32 seq, U32 arg) {
    sink(arg);
    if (arg > 10) {
      cmdResponse_out(op, seq, Fw::CmdResponse::VALIDATION_ERROR);
    }
  }
  void F_cmdHandler(FwOpcodeType op, U32 seq, U32 arg) {
    for (U32 i = 0; i < arg; ++i) {
      if (i > 10) {
        cmdResponse_out(op, seq, Fw::CmdResponse::EXECUTION_ERROR);
        break;
      }
      if (i > 5) {
        cmdResponse_out(op, seq, Fw::CmdResponse::EXECUTION_ERROR);
      }
      sink(i);
    }
  }
  void G_cmdHandler(FwOpcodeType op, U32 seq, U32 arg) {
    Fw::CmdResponse resp(Fw::CmdResponse::EXECUTION_ERROR);
    if (arg > 10) {
      cmdResponse_out(op, seq, resp);
    }
    if (arg > 11) {
      cmdResponse_out(op, seq, arg > 12 ? Fw::CmdResponse::EXECUTION_ERROR : Fw::CmdResponse::OK);
    }
    if (arg > 13) {
      cmdResponse_out(op, seq, Fw::CmdResponse::OK);
    }
    sink(arg);
  }
  void H_cmdHandler(FwOpcodeType op, U32 seq, U32 arg) {
    if (arg > 10) {
      cmdResponse_out(op, seq, Fw::CmdResponse::VALIDATION_ERROR);
      if (arg > 20) { return; }
    }
    if (arg > 30) {
      cmdResponse_out(op, seq, Fw::CmdResponse::VALIDATION_ERROR);
      goto done;
    }
    if (arg > 1) {
      sink(0);
    } else if (arg > 2) {
      cmdResponse_out(op, seq, Fw::CmdResponse::VALIDATION_ERROR);
    }
    sink(arg);
  done:
    sink(arg);
  }
  void I_cmdHandler(FwOpcodeType op, U32 seq, U32 arg) {
    switch (arg) {
      case 1:
        if (arg > 10) {
          cmdResponse_out(op, seq, Fw::CmdResponse::VALIDATION_ERROR);
        }
        break;
      case 2: {
        if (arg > 10) {
          cmdResponse_out(op, seq, Fw::CmdResponse::VALIDATION_ERROR);
        }
        break;
      }
      default:
        break;
    }
  }
  void helper(FwOpcodeType op, U32 seq, U32 arg) {
    if (arg > 10) {
      cmdResponse_out(op, seq, Fw::CmdResponse::VALIDATION_ERROR);
    }
    sink(arg);
  }
  void schedIn_handler(U32 ctx) {
    if (ctx > 10) {
      cmdResponse_out(0, 0, Fw::CmdResponse::EXECUTION_ERROR);
    }
    sink(ctx);
  }
  void x_internalInterfaceHandler(U32 ctx) {
    if (ctx > 10) {
      cmdResponse_out(0, 0, Fw::CmdResponse::EXECUTION_ERROR);
    }
    sink(ctx);
  }
  void J_cmdHandler(FwOpcodeType op, U32 seq, U32 arg) {
    if (arg > 10) {
      cmdResponse_out(op, seq, Fw::CmdResponse::VALIDATION_ERROR);
    }
  }
  void K_cmdHandler(FwOpcodeType op, U32 seq, U32 arg) {
    if (arg > 10) {
      auto l = [this, op, seq] { cmdResponse_out(op, seq, Fw::CmdResponse::VALIDATION_ERROR); };
      l();
    }
    sink(arg);
  }
};
