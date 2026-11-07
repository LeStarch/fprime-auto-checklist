typedef unsigned int U32;
U32 size();
void sink(U32);
struct It { It& operator++(); It& operator--(); bool operator!=(const It&) const; U32 operator*() const; };
struct Cont { It begin(); It end(); };
void cases(Cont c, U32 n, U32 (&arr)[8]) {
  U32 j = 0;
  for (U32 i = 0; i < n; i++) { }
  for (U32 i = 0; i < size(); i++) { }
  for (U32 i = 0; i < n; i++, j++) { }
  for (U32 i = 0; i < n; i++, sink(i)) { }
  for (U32 i = size(); i < n; i++) { }
  for (j = size(); j < n; j++) { }
  for (U32 i = 0, k = 0; i < n; i++) { (void)k; }
  for (It it = c.begin(); it != c.end(); ++it) { }
  for (It it = c.begin(); it != c.end(); it.operator++()) { }
  for (It it = c.begin(); it != c.end(); --it) { }
  for (U32 i = 0; i < n; i += 2) { }
  for (U32 i = 0; i < n; i = i + 1) { }
  for (U32 i = 0; i < n; arr[i++] = 0) { }
  for (U32 i = 0; i < n; ++i, --j) { }
  for (j = 0; j < n; j++) { }
  for (U32 i = 0; i < n && j < n; i++, j++) { }
  for (sink(0); j < n; j++) { }
  for (U32 i = 0; i < n; i++, n--) { }
  for (U32 i = 0; i < n; (void)i++) { }
  for (U32 i = 0; i < n; i++) { for (U32 k = 0; k < n; k++, i++) { } }
  for (U32 i = 0; i < n; i = static_cast<U32>(i + 1)) { }
  for (U32 i = 0; i < n; i++) { j++; }
}
