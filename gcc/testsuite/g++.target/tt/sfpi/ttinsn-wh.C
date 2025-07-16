// { dg-options "-mcpu=tt-wh -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }

namespace ckernel {
  extern unsigned instrn_buffer[];
}
#include <lltt.h>
#include <sfpi.h>

void foo (uint32_t v) {
  lltt::insn(0x12345678);
  lltt::insn(v);
}
// { dg-final { scan-assembler {\n_Z3foom:\n\t.ttinsn	305419896\n\tlui	a5,%hi\(_ZN7ckernel13instrn_bufferE\)\n\tsw	a0,%lo\(_ZN7ckernel13instrn_bufferE\)\(a5\)\n\tret\n} } }

using namespace sfpi;

// { dg-final { scan-assembler {\n_Z3barv:\n\tSFPADD	L2, L10, L2, L2, 0\n\tSFPNOP\n\t.ttinsn	305419896\n\tSFPADD	L0, L10, L2, L2, 0\n\tret\n} } }
void bar () {
  vFloat c = l_reg[LRegs::LReg2];
  vFloat d = c + c;
  // needs NOP
  lltt::insn(0x12345678);
  vFloat e = d + d;
  l_reg[LRegs::LReg0] = e;
}

void baz () {
  vFloat c = l_reg[LRegs::LReg2];
  vFloat d = c + c;
  lltt::insn<true>(0x12345678);
  // no need for NOP
  vFloat e = d + d;
  l_reg[LRegs::LReg0] = e;
}


// { dg-final { scan-assembler {\n_Z3bazv:\n\tSFPADD	L2, L10, L2, L2, 0\n\t.ttinsn	305419896\n\tSFPADD	L0, L10, L2, L2, 0\n\tret\n} } }
