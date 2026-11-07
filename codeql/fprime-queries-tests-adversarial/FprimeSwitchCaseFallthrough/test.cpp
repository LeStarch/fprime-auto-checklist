void sink(int);
[[noreturn]] void die();
#define BREAK break;
#define FALL_COMMENT /* fallthrough */
void cases(int x) {
  switch (x) {
    case 0:
      sink(0);
      [[fallthrough]];
    case 1:
      sink(1);
      // falls through intentionally
    case 2:
      sink(2); // FALLTHRU
    case 3:
      sink(3);
      /* Fall-Through */
    case 4:
      sink(4);
      die();
    case 5:
      sink(5);
      BREAK
    case 6: {
      sink(6);
      { break; }
    }
    case 7:
      sink(7);
      FALL_COMMENT
    case 8:
      if (x) { break; } else { return; }
    case 9:
      sink(9);
      label: break;
    case 10:
      { }
    case 11:
      sink(11);
      while (true) { return; }
    case 12:
      sink(12);
      switch (x) { default: break; }
    case 13:
      sink(13);
      /* fallthrough */ case 14:
      sink(14);
    default:
      break;
  }
}
