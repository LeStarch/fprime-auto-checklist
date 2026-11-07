typedef unsigned int U32;
struct Comp {
  U32 m_count; U32 m_warnThrottle; bool m_state;
  void log_WARNING_HI_Bad(U32);
  void log_WARNING_LO_Throttled(U32) { if (m_warnThrottle >= 10) { return; } m_warnThrottle++; }
  void log_WARNING_LO_Throttled_ThrottleClear() { m_warnThrottle = 0; }
  void log_ACTIVITY_LO_Fine();
  void log_DIAGNOSTIC_Dbg();
  void log_COMMAND_Done();
  void log_FATAL_Boom();
  void tlmWrite_Count(U32);
  void schedIn_handler(U32 ctx) {
    log_WARNING_HI_Bad(1);
    log_WARNING_LO_Throttled(1);
    log_WARNING_LO_Throttled_ThrottleClear();
    log_ACTIVITY_LO_Fine();
    log_DIAGNOSTIC_Dbg();
    log_COMMAND_Done();
    log_FATAL_Boom();
    tlmWrite_Count(1);
    if (m_state) { log_WARNING_HI_Bad(2); }
    if (ctx > 3) { log_WARNING_HI_Bad(3); }
    if (ctx > 3 && m_count) { log_WARNING_HI_Bad(4); }
    switch (m_count) { case 1: log_WARNING_HI_Bad(5); break; default: break; }
    switch (ctx) { case 1: log_WARNING_HI_Bad(6); break; default: break; }
    bool s = m_state; if (s) { log_WARNING_HI_Bad(7); }
    for (U32 i = 0; i < m_count; i++) { log_WARNING_HI_Bad(8); }
    log_WARNING_HI_Bad(m_state ? 1u : 0u);
    while (m_state) { log_WARNING_HI_Bad(9); m_state = false; }
    if (this->m_state) { log_WARNING_HI_Bad(10); }
    auto l = [this] { log_WARNING_HI_Bad(11); }; l();
  }
  void run_handler(U32 ctx) { log_WARNING_HI_Bad(1); }
  void Run_handler(U32 ctx) { log_WARNING_HI_Bad(1); }
  void cycle_handler(U32 ctx) { log_WARNING_HI_Bad(1); }
  void onTick_handler(U32 ctx) { log_WARNING_HI_Bad(1); }
  void schedIn_preMsgHook(U32 ctx) { log_WARNING_HI_Bad(1); }
  void tickle(U32 ctx) { log_WARNING_HI_Bad(1); }
  void helper() { log_WARNING_HI_Bad(1); }
  void rateGroup_handler(U32 ctx) { helper(); }
  void mySched_handler(U32 ctx) { log_WARNING_HI_Bad(1); }
  void CYCLE_handler(U32 ctx) { log_WARNING_HI_Bad(1); }
};
void log_WARNING_HI_Free(U32);
void free_schedIn_handler(U32 ctx) { log_WARNING_HI_Free(1); }
