typedef unsigned int U32;
typedef unsigned long FwSizeType;
typedef int I32;
namespace Fw { void SwAssert(int); }
#define FW_ASSERT(cond, ...) ((cond) ? static_cast<void>(0) : Fw::SwAssert(__LINE__))
struct Buffer { FwSizeType getSize() const; FwSizeType getBuffLength() const; U32 getData() const; };
void sink(FwSizeType);
struct Comp {
  FwSizeType getSize() const;
  void unchecked(Buffer& b) { sink(b.getSize() - 4); }
  void checked(Buffer& b) { if (b.getSize() < 4) { return; } sink(b.getSize() - 4); }
  void weakCheck(Buffer& b) { if (b.getSize() >= 2) { sink(b.getSize() - 4); } }
  void exactCheck(Buffer& b) { if (b.getSize() >= 3) { sink(b.getSize() - 4); } }
  void variableBound(Buffer& b, FwSizeType n) { if (b.getSize() > n) { sink(b.getSize() - 4); } }
  void otherReceiver(Buffer& b, Buffer& o) { if (o.getSize() >= 4) { sink(b.getSize() - 4); } }
  void viaLocal(Buffer& b) { FwSizeType s = b.getSize(); if (s < 4) { return; } sink(b.getSize() - 4); }
  void checkAfter(Buffer& b) { sink(b.getSize() - 4); if (b.getSize() < 4) { return; } }
  void thisReceiver() { if (getSize() < 4) { return; } sink(this->getSize() - 4); }
  void thisUnchecked() { sink(getSize() - 4); }
  void otherAccessor(Buffer& b) { if (b.getSize() >= 4) { sink(b.getBuffLength() - 4); } }
  void nonSizeAccessor(Buffer& b) { sink(b.getData() - 4); }
  void reversed(Buffer& b, FwSizeType x) { sink(x - b.getSize()); }
  void nonConstant(Buffer& b, FwSizeType n) { sink(b.getSize() - n); }
  void signedResult(Buffer& b) { sink(static_cast<FwSizeType>(static_cast<I32>(b.getSize()) - 4)); }
  void nested(Buffer& b) { sink(b.getSize() + 1 - 4); }
  void sizeofSub(Buffer& b) { sink(b.getSize() - sizeof(U32)); }
  void asserted(Buffer& b) { FW_ASSERT(b.getSize() >= 4); sink(b.getSize() - 4); }
  void equality(Buffer& b) { if (b.getSize() == 4) { sink(b.getSize() - 4); } }
  void helperCheck(Buffer& b) { check(b); sink(b.getSize() - 4); }
  void check(Buffer& b) { FW_ASSERT(b.getSize() >= 4); }
  void twoSubs(Buffer& b) { if (b.getSize() >= 8) { sink(b.getSize() - 4); sink(b.getSize() - 12); } }
  void lambda(Buffer& b) { auto l = [&b] { return b.getSize() - 4; }; if (b.getSize() >= 4) { sink(l()); } }
  void pointer(Buffer* b) { if (b->getSize() < 4) { return; } sink(b->getSize() - 4); }
  void mixedReceivers(Buffer* b, Buffer& r) { if (b->getSize() >= 4) { sink(r.getSize() - 4); } }
  void u32Sub(Buffer& b) { U32 x = static_cast<U32>(b.getSize()) - 4; sink(x); }
  void paren(Buffer& b) { sink((b.getSize()) - 4); }
};
