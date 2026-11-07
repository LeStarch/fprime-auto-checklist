typedef unsigned int U32;
extern "C" int open(const char*, int);
extern "C" int close(int);
extern "C" long read(int, void*, unsigned long);
extern "C" int pthread_mutex_lock(void*);
extern "C" int pthread_mutex_unlock(void*);
extern "C" int sem_wait(void*);
extern "C" int sem_post(void*);
long write(int fd, const char* s);
namespace Os {
struct Mutex { void lock(); void unLock(); void lockData(); void unlockData(); };
struct ScopedLock { ScopedLock(Mutex&); ~ScopedLock(); };
struct File { int open(const char*, int); void close(); int read(void*, U32&); int readDirectory(); };
}
struct Reader { int read(void*, U32); };
void sink(int);
struct Comp {
  Os::Mutex m_mutex;
  void* m_pm;
  void* m_sem;
  Os::File m_file;
  Reader m_reader;
  void schedIn_handler(U32 ctx) {
    int fd = open("x", 0);
    close(fd);
    U32 n; m_file.read(nullptr, n);
    m_reader.read(nullptr, 1);
    write(1, "x");
    auto l = [this] { int f = open("y", 0); close(f); };
    l();
  }
  void locked() {
    m_mutex.lock();
    int fd = open("x", 0);
    m_mutex.unLock();
    close(fd);
  }
  void lockedData() {
    m_mutex.lockData();
    m_file.readDirectory();
    m_mutex.unlockData();
  }
  void pthread() {
    pthread_mutex_lock(m_pm);
    read(0, nullptr, 1);
    pthread_mutex_unlock(m_pm);
    read(0, nullptr, 1);
  }
  void sem() {
    sem_wait(m_sem);
    sink(0);
    sem_post(m_sem);
    U32 n; m_file.read(nullptr, n);
  }
  void branchLock(bool b) {
    if (b) { m_mutex.lock(); }
    int fd = open("x", 0);
    if (b) { m_mutex.unLock(); }
    close(fd);
  }
  void conditionalUnlock(bool b) {
    m_mutex.lock();
    if (b) { m_mutex.unLock(); }
    read(0, nullptr, 1);
  }
  void loopAccess() {
    m_mutex.lock();
    for (int i = 0; i < 3; ++i) { read(0, nullptr, 1); }
    m_mutex.unLock();
  }
  void scoped() {
    Os::ScopedLock lock(m_mutex);
    read(0, nullptr, 1);
  }
  void lambdaAfterLock() {
    m_mutex.lock();
    auto l = [] { read(0, nullptr, 1); };
    l();
    m_mutex.unLock();
  }
  void helperAccess() { read(0, nullptr, 1); }
  void lockThenHelper() {
    m_mutex.lock();
    helperAccess();
    m_mutex.unLock();
  }
  void earlyReturn(bool b) {
    m_mutex.lock();
    if (b) { m_mutex.unLock(); return; }
    read(0, nullptr, 1);
    m_mutex.unLock();
  }
  void unlockOnlyInLoop(int n) {
    m_mutex.lock();
    while (n--) { if (n == 1) { m_mutex.unLock(); } }
    read(0, nullptr, 1);
  }
  void writeUserFn() {
    m_mutex.lock();
    write(1, "x");
    m_mutex.unLock();
  }
};
