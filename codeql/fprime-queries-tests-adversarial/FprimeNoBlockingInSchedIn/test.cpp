extern "C" unsigned int sleep(unsigned int);
extern "C" int usleep(unsigned int);
namespace Os { struct Mutex { void lock(); void unLock(); }; }
struct Comp {
  Os::Mutex m_mutex;
  void schedIn_handler(unsigned int ctx) { usleep(1); m_mutex.lock(); }
  void rateGroup_schedIn_handler(unsigned int ctx) { sleep(1); }
  void schedInHandler(unsigned int ctx) { sleep(1); }
  void schedIn_preMsgHook(unsigned int ctx) { sleep(1); }
  void schedIn_handlerBase(unsigned int ctx) { sleep(1); }
  void SchedIn_handler(unsigned int ctx) { sleep(1); }
  void helper() { sleep(1); }
  void run_schedIn_handler(unsigned int ctx) { helper(); auto l = [] { sleep(1); }; l(); }
  void schedin_handler(unsigned int ctx) { sleep(1); }
};
void schedIn_handler(unsigned int ctx) { sleep(1); }
