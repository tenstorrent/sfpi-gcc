// { dg-options "-mcpu=tt-bh -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }

namespace ckernel{
    unsigned *instrn_buffer;
}
#include <sfpi.h>

using namespace sfpi;

namespace recip {
void foo () {
  // Basic test
  vFloat a = l_reg[LRegs::LReg0];
  vFloat r = approx_recip (a);
  l_reg[LRegs::LReg1] = r;
}
// { dg-final { scan-assembler {\n_ZN5recip3fooEv:\n\tSFPARECIP	L1, L0, 0\n\tret\n} } }

void bar () {
  // Live value test
  vFloat a = l_reg[LRegs::LReg0];
  vFloat b = l_reg[LRegs::LReg1];
  vFloat c = l_reg[LRegs::LReg2];
  
  vFloat r;
  v_if (a < b) {
    r = c;
  } v_else {
    r = approx_recip (a);
  } v_endif;

  l_reg[LRegs::LReg3] = r;
}
// { dg-final { scan-assembler {\n_ZN5recip3barEv:\n\tSFPMAD	L1, L1, L11, L0, 0\n\tSFPNOP\n\tSFPSETCC	L1, 0, 0\n\tSFPCOMPC\n\tSFPARECIP	L2, L0, 0\n\tSFPMOV	L3, L2, 2\n\tSFPENCC	3, 10\n\tret\n} } }
}

namespace negrecip {
void foo () {
  // Basic test
  vFloat a = l_reg[LRegs::LReg0];
  vFloat r = approx_recip<false> (a);
  l_reg[LRegs::LReg1] = r;
}
// { dg-final { scan-assembler {\n_ZN8negrecip3fooEv:\n\tSFPARECIP	L1, L0, 1\n\tret\n} } }

void bar () {
  // Live value test
  vFloat a = l_reg[LRegs::LReg0];
  vFloat b = l_reg[LRegs::LReg1];
  vFloat c = l_reg[LRegs::LReg2];
  
  vFloat r;
  v_if (a < b) {
    r = c;
  } v_else {
    r = approx_recip<false> (a);
  } v_endif;

  l_reg[LRegs::LReg3] = r;
}
// { dg-final { scan-assembler {\n_ZN8negrecip3barEv:\n\tSFPMAD	L1, L1, L11, L0, 0\n\tSFPNOP\n\tSFPSETCC	L1, 0, 0\n\tSFPCOMPC\n\tSFPARECIP	L2, L0, 1\n\tSFPMOV	L3, L2, 2\n\tSFPENCC	3, 10\n\tret\n} } }
}

namespace expon {
void foo () {
  // Basic test
  vFloat a = l_reg[LRegs::LReg0];
  vFloat r = approx_exp (a);
  l_reg[LRegs::LReg1] = r;
}
// { dg-final { scan-assembler {\n_ZN5expon3fooEv:\n\tSFPARECIP	L1, L0, 2\n\tret\n} } }

void bar () {
  // Live value test
  vFloat a = l_reg[LRegs::LReg0];
  vFloat b = l_reg[LRegs::LReg1];
  vFloat c = l_reg[LRegs::LReg2];
  
  vFloat r;
  v_if (a < b) {
    r = c;
  } v_else {
    r = approx_exp (a);
  } v_endif;

  l_reg[LRegs::LReg3] = r;
}
// { dg-final { scan-assembler {\n_ZN5expon3barEv:\n\tSFPMAD	L1, L1, L11, L0, 0\n\tSFPNOP\n\tSFPSETCC	L1, 0, 0\n\tSFPCOMPC\n\tSFPARECIP	L2, L0, 2\n\tSFPMOV	L3, L2, 2\n\tSFPENCC	3, 10\n\tret\n} } }
}
