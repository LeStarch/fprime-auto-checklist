extern "C" unsigned int sleep(unsigned int);
extern "C" int usleep(unsigned int);
extern "C" int select(int, void*, void*, void*, void*);
int wait(int);
#define DELAY_MS(ms) usleep((ms) * 1000)
namespace Os { struct Mutex { void lock(); void unLock(); }; struct Task { static void delay(unsigned int); }; struct Queue { int receiveBlocking(); }; }
namespace Other { struct Mutex { void lock(); }; }
struct Sock { int select(int); int recv(); };
struct Dev {
  Os::Mutex m_mutex;
  Other::Mutex m_other;
  Sock m_sock;
  void uart_isr() {
    sleep(1);
    ::usleep(1);
    DELAY_MS(2);
    m_mutex.lock();
    this->m_mutex.lock();
    Os::Task::delay(1);
    m_other.lock();
    m_sock.select(1);
    m_sock.recv();
    wait(1);
    Os::Queue q; q.receiveBlocking();
    auto l = [] { sleep(1); };
    l();
    unsigned int (*fp)(unsigned int) = sleep;
    fp(1);
    select(0, nullptr, nullptr, nullptr, nullptr);
  }
  void helper() { sleep(1); }
  void timer_isr() { helper(); }
  void notIsr() { sleep(1); }
};
template <typename T> void tmpl_isr(T t) { sleep(t); }
void inst() { tmpl_isr(1u); }
__attribute__((interrupt)) void attributed() { sleep(1); }
