typedef unsigned int U32;
struct Comp {
  U32 m_limit;
  void run(U32 n, bool c, U32 (&arr)[16]) {
    U32 i = 0;
    for (U32 a = 0; a < 8; a++) { }
    for (i = 0; i < n; i++) { }
    for (i = 0; i < n; i++) { n++; }
    while (i < 10) { if (c) { i++; } }
    while (i < 10) { i += 2; }
    while (i < 10) { i = i + 1; }
    while (i < 10) { i = i + n; }
    while (true) { if (c) { break; } }
    while (1) { if (c) { break; } }
    for (;;) { if (c) { break; } }
    while (c) { c = false; }
    while (i > 0) { i--; }
    while (i > 0) { i -= 1; }
    while (i > 0) { i = i - 1; }
    while (i != 0) { i--; }
    while (i < 10 && c) { i++; }
    for (i = 0; i < 'z'; i++) { }
    const U32 limit = 4;
    for (i = 0; i < limit; i++) { }
    for (i = 0; i < m_limit; i++) { }
    for (i = 0; i < m_limit; i++) { m_limit--; }
    for (i = 0; i < 10; ) { arr[i++] = 0; }
    for (i = 0; i < 10; i++) { if (c) { break; } }
    while (i < 10) { if (c) { continue; } i++; }
    do { i++; } while (i < 10);
    while (10 > i) { i++; }
    while (i < n) { i++; n--; }
    while (i < 10) { i++; i--; }
    for (U32 v : arr) { (void)v; }
    // @non-terminating@
    while (c) { }
    for (i = 0; i <= 10; i++) { }
    for (i = 0; i < 10; i += 0) { }
    for (i = 0; i < 10; i = i + 0) { }
    for (i = 10; i > 0; i = i - 2) { }
    for (i = 0; i < 10; i *= 2) { }
    for (U32 x = 0, y = 0; x < 10 && y < 10; x++) { y++; }
    for (i = 0; i < 10; i++) { for (U32 j = 0; j < i; j++) { } }
    while (c && i < 10) { c = false; }
    while (i < 10) { i++; if (c) { i = 0; } }
    for (i = 0; i < sizeof(arr); i++) { }
    for (i = 0; i < n + 1; i++) { }
  }
};
template <typename T> void tmpl(T n) { for (T i = 0; i < n; i++) { } while (true) { break; } }
void inst() { tmpl(3u); }
