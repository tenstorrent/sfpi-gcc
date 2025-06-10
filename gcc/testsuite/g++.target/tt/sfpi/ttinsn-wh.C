// { dg-options "-mcpu=tt-wh -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }

namespace ckernel {
  extern unsigned instrn_buffer[];
}
#define TT_OP(opcode, params) ((opcode << 24) + params)
#define TT_OP_REPLAY(start_idx, len, execute_while_loading, load_mode) \
    TT_OP(0x04, (((start_idx) << 14) + ((len) << 4) + ((execute_while_loading) << 1) + ((load_mode) << 0)))

#include <lltt.h>
#include <sfpi.h>

void foo (uint32_t v) {
  lltt::insn(0x12345678);
  lltt::insn(v);
}
// { dg-final { scan-assembler {\n_Z3foom:\n\t.ttinsn	305419896\n\tlui	a5,%hi\(__instrn_buffer\)\n\tsw	a0,%lo\(__instrn_buffer\)\(a5\)\n\tret\n} } }

using namespace sfpi;

void foo () {
  vFloat c = l_reg[LRegs::LReg2];
  vFloat d = c + c;
  // needs NOP
  vFloat e = d + d;
  l_reg[LRegs::LReg0] = e;
}
// { dg-final { scan-assembler {\n_Z3foov:\n\tSFPADD	L2, L10, L2, L2, 0\n\tSFPNOP\n\tSFPADD	L0, L10, L2, L2, 0\n\tret\n} } }

void bar () {
  vFloat c = l_reg[LRegs::LReg2];
  vFloat d = c + c;
  lltt::insn(0x12345678);
  // no need for NOP
  vFloat e = d + d;
  l_reg[LRegs::LReg0] = e;
}
// { dg-final { scan-assembler {\n_Z3barv:\n\tSFPADD	L2, L10, L2, L2, 0\n\t.ttinsn	305419896\n\tSFPADD	L0, L10, L2, L2, 0\n\tret\n} } }

void baz (uint32_t v) {
  vFloat c = l_reg[LRegs::LReg2];
  vFloat d = c + c;
  lltt::insn(v);
  // no need for NOP
  vFloat e = d + d;
  l_reg[LRegs::LReg0] = e;
}
// { dg-final { scan-assembler {\n_Z3bazm:\n\tSFPADD	L2, L10, L2, L2, 0\n\tlui	a5,%hi\(__instrn_buffer\)\n\tsw	a0,%lo\(__instrn_buffer\)\(a5\)\n\tSFPADD	L0, L10, L2, L2, 0\n\tret\n} } }
