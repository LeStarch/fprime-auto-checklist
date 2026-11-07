void sink(int);
#define RETURN_IF(c) if (c) return;
#define LOOP_BODY sink(1);
void basics(int x, int (&arr)[3]) {
  if (x) sink(0); else if (x > 1) sink(1); else sink(2);
  for (int i = 0; i < 3; ++i) ;
  while (x--) sink(x);
  do sink(x); while (x++ < 3);
  for (int v : arr) sink(v);
  if (x) if (x > 2) sink(3);
  if (x) { sink(4); } else if (x < 0) sink(5);
  RETURN_IF(x == 7)
  for (int i = 0; i < 3; ++i) LOOP_BODY
  if (x) { sink(8); } else { }
  if (x) { } else ;
  auto l = [x] { if (x) return 1; return 0; };
  l();
}
template <typename T> void tmpl(T t) { if (t) sink(1); while (t) --t; }
void inst() { tmpl(1); tmpl(2u); }
