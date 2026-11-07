typedef unsigned int U32;
#define INVOKE(x) tick_internalInterfaceHandler(x)
struct Comp {
  void tick_internalInterfaceHandler(U32 v);
  void tick_internalInterfaceInvoke(U32 v);
  void doDispatch(U32 v) { tick_internalInterfaceHandler(v); }
  void Dispatch_handler(U32 v) { this->tick_internalInterfaceHandler(v); }
  void preDispatchCheck(U32 v) { tick_internalInterfaceHandler(v); }
  void run_handler(U32 v) { tick_internalInterfaceHandler(v); }
  void viaThis(U32 v) { this->tick_internalInterfaceHandler(v); }
  void viaMacro(U32 v) { INVOKE(v); }
  void viaInvoke(U32 v) { tick_internalInterfaceInvoke(v); }
  void viaPointer(U32 v) { void (Comp::*p)(U32) = &Comp::tick_internalInterfaceHandler; (this->*p)(v); }
  void inLambda(U32 v) { auto l = [this, v] { tick_internalInterfaceHandler(v); }; l(); }
  void dispatchInLambda(U32 v) { auto l = [this, v] { tick_internalInterfaceHandler(v); }; l(); }
  template <typename T> void tmpl(T v) { tick_internalInterfaceHandler(v); }
  void inst() { tmpl(1u); tmpl(2); }
  void (Comp::*stored)(U32);
  void store() { stored = &Comp::tick_internalInterfaceHandler; }
};
void freeCaller(Comp& c) { c.tick_internalInterfaceHandler(1); }
void freeDispatch(Comp& c) { c.tick_internalInterfaceHandler(1); }
