// { dg-options "-mcpu=tt-bh -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }

// Verify we notice a - b is a + -1.0 * b

namespace ckernel{
    unsigned *instrn_buffer;
}
#include <sfpi.h>

using namespace sfpi;

void sub1() {
  vFloat a = l_reg[LRegs::LReg0];
  vFloat b = l_reg[LRegs::LReg1];
  vFloat r;

  r = a - b;
  
  l_reg[LRegs::LReg3] = r;
}
// { dg-final { scan-assembler {\n_Z4sub1v:\n\tSFPADD	L3, L10, L0, L1, 2\n\tret\n} } }

void sub2() {
  vFloat a = l_reg[LRegs::LReg0];
  vFloat b = l_reg[LRegs::LReg1];

  b -= a;
  
  l_reg[LRegs::LReg3] = b;
}
// { dg-final { scan-assembler {\n_Z4sub2v:\n\tSFPADD	L3, L10, L1, L0, 2\n\tret\n} } }

void sub3() {
  vFloat a = l_reg[LRegs::LReg0];
  vFloat b = l_reg[LRegs::LReg1];
  vFloat r;

  r = -a - b;
  
  l_reg[LRegs::LReg3] = r;
}
// { dg-final { scan-assembler {\n_Z4sub3v:\n\tSFPADD	L3, L10, L0, L1, 3\n\tret\n} } }

void sub4() {
  vFloat a = l_reg[LRegs::LReg0];
  vFloat b = l_reg[LRegs::LReg1];
  vFloat r;

  v_if (a < b) {
    r = a - b;
  } v_else {
    r = b - a;
  } v_endif;
  
  l_reg[LRegs::LReg3] = r;
}
// { dg-final { scan-assembler {\n_Z4sub4v:\n\tSFPMAD	L3, L1, L11, L0, 0\n\tSFPNOP\n\tSFPSETCC	L3, 0, 0\n\tSFPADD	L3, L10, L0, L1, 2\n\tSFPCOMPC\n\tSFPADD	L3, L10, L1, L0, 2\n\tSFPENCC	3, 10\n\tret\n} } }

void sub5() {
  vFloat a = l_reg[LRegs::LReg0];
  vFloat b = l_reg[LRegs::LReg1];

  v_if (a < b) {
    b -= a;
  } v_else {
    b -= -a;
  } v_endif;
  
  l_reg[LRegs::LReg3] = b;
}
// { dg-final { scan-assembler {\n_Z4sub5v:\n\tSFPMAD	L2, L1, L11, L0, 0\n\tSFPNOP\n\tSFPSETCC	L2, 0, 0\n\tSFPADD	L1, L10, L1, L0, 2\n\tSFPCOMPC\n\tSFPMOV	L0, L0, 1\n\tSFPADD	L1, L10, L1, L0, 2\n\tSFPENCC	3, 10\n\tSFPMOV	L3, L1, 2\n\tret\n} } }

void sub6() {
  vFloat a = l_reg[LRegs::LReg0];
  vFloat b = l_reg[LRegs::LReg1];
  vFloat r;

  v_if (a < b) {
    r = -a - b;
  } v_else {
    r = a - b;
  } v_endif;
  
  l_reg[LRegs::LReg3] = r;
}
// { dg-final { scan-assembler {\n_Z4sub6v:\n\tSFPMAD	L3, L1, L11, L0, 0\n\tSFPNOP\n\tSFPSETCC	L3, 0, 0\n\tSFPADD	L3, L10, L0, L1, 3\n\tSFPCOMPC\n\tSFPADD	L3, L10, L0, L1, 2\n\tSFPENCC	3, 10\n\tret\n} } }
