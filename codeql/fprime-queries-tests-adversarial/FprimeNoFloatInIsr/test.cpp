float g;
double h;
void handle_isr(float a, int i) {
  float f = a * 2.0f;
  g = f;
  g += 1.0f;
  g++;
  -g;
  bool c = a < 1.0f;
  float t = c ? a : g;
  int n = (int)a;
  int m = a;
  float u = (a, 1.0f);
  int k = i + 1;
  auto l = [a] { return a * 2.0f; };
  (void)t; (void)n; (void)m; (void)u; (void)k; (void)l;
}
void ISR_Timer() { g = g + 1.0f; }
void timerisr() { g = g + 1.0f; }
void irqHandler() { g = g + 1.0f; }
void irq_handler() { g = g + 1.0f; }
void myInterruptHandler() { g = g + 1.0f; }
__attribute__((interrupt)) void attributed() { g = g + 1.0f; }
void notAnIsr() { g = g + 1.0f; }
struct Dev { void uart_isr() { h = h / 2.0; } };
void helper_for_isr() { h = h * 2.0; }
void isr_calls_helper() { helper_for_isr(); }
