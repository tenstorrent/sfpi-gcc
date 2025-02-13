// { dg-options "-mcpu=tt-gs -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }

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
// { dg-final { scan-assembler {\n_Z4sub1v:\n\tSFPMAD	L3, L11, L1, L0, 0\n\tTTNOP\n\tTTNOP\n\tret\n} } }

void sub2() {
  vFloat a = l_reg[LRegs::LReg0];
  vFloat b = l_reg[LRegs::LReg1];

  b -= a;
  
  l_reg[LRegs::LReg3] = b;
}
// { dg-final { scan-assembler {\n_Z4sub2v:\n\tSFPMAD	L3, L11, L0, L1, 0\n\tTTNOP\n\tTTNOP\n\tret\n} } }

void sub3() {
  vFloat a = l_reg[LRegs::LReg0];
  vFloat b = l_reg[LRegs::LReg1];
  vFloat r;

  r = -a - b;
  
  l_reg[LRegs::LReg3] = r;
}
// { dg-final { scan-assembler {\n_Z4sub3v:\n\tSFPMOV	L0, L0, 1\n\tTTNOP\n\tTTNOP\n\tSFPMAD	L3, L11, L1, L0, 0\n\tTTNOP\n\tTTNOP\n\tret\n} } }

void sub4() {
  vFloat a = l_reg[LRegs::LReg0];
  vFloat b = l_reg[LRegs::LReg1];
  vFloat r;

  v_if (a < b) {
    r = a - b;
  } v_else {
    r = b;
  } v_endif;
  
  l_reg[LRegs::LReg3] = r;
}
// { dg-final { scan-assembler {\n_Z4sub4v:\n\tSFPMAD	L3, L1, L11, L0, 0\n\tTTNOP\n\tTTNOP\n\tSFPSETCC	L3, 0, 0\n\tTTNOP\n\tSFPMAD	L3, L11, L1, L0, 0\n\tTTNOP\n\tTTNOP\n\tSFPCOMPC\n\tTTNOP\n\tSFPMOV	L3, L1, 0\n\tTTNOP\n\tTTNOP\n\tSFPENCC	3, 10\n\tTTNOP\n\tret\n} } }

void sub5() {
  vFloat a = l_reg[LRegs::LReg0];
  vFloat b = l_reg[LRegs::LReg1];

  v_if (a < b) {
    b -= a;
  } v_endif;
  
  l_reg[LRegs::LReg3] = b;
}
// { dg-final { scan-assembler {\n_Z4sub5v:\n\tSFPMAD	L3, L1, L11, L0, 0\n\tTTNOP\n\tTTNOP\n\tSFPSETCC	L3, 0, 0\n\tTTNOP\n\tSFPMAD	L1, L11, L0, L1, 0\n\tTTNOP\n\tTTNOP\n\tSFPPUSHC\n\tSFPENCC	3, 2\n\tTTNOP\n\tSFPMOV	L3, L1, 0\n\tTTNOP\n\tTTNOP\n\tSFPPOPC\n\tTTNOP\n\tSFPENCC	3, 10\n\tTTNOP\n\tret\n} } }

void sub6() {
  vFloat a = l_reg[LRegs::LReg0];
  vFloat b = l_reg[LRegs::LReg1];
  vFloat r;

  v_if (a < b) {
    r = -a - b;
  } v_else {
    r = a;
  } v_endif;
  
  l_reg[LRegs::LReg3] = r;
}
// { dg-final { scan-assembler {\n_Z4sub6v:\n\tSFPMAD	L2, L1, L11, L0, 0\n\tTTNOP\n\tTTNOP\n\tSFPSETCC	L2, 0, 0\n\tTTNOP\n\tSFPMOV	L2, L0, 1\n\tTTNOP\n\tTTNOP\n\tSFPMAD	L3, L11, L1, L2, 0\n\tTTNOP\n\tTTNOP\n\tSFPCOMPC\n\tTTNOP\n\tSFPMOV	L3, L0, 0\n\tTTNOP\n\tTTNOP\n\tSFPENCC	3, 10\n\tTTNOP\n\tret\n} } }
