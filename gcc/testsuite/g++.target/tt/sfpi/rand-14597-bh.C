// { dg-options "-mcpu=tt-bh -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }

namespace ckernel{
    unsigned *instrn_buffer;
}
#include <sfpi.h>

using namespace sfpi;

void foo () {
  // Basic test
  vInt r = rand ();
  l_reg[LRegs::LReg0] = r;
}
// { dg-final { scan-assembler {\n_Z3foov:\n\tSFPMOV	L0,L9,8\n\tret\n} } }

void bar () {
  // Live value test
  vFloat a = l_reg[LRegs::LReg0];
  vFloat b = l_reg[LRegs::LReg1];
  vInt c = l_reg[LRegs::LReg2];
  
  vInt r;
  v_if (a < b) {
    r = c;
  } v_else {
    r = rand ();
  } v_endif;
  
  l_reg[LRegs::LReg0] = r;
}
// { dg-final { scan-assembler {\n_Z3barv:\n\tSFPMAD	L0, L1, L11, L0, 0\n\tSFPNOP\n\tSFPSETCC	L0, 0, 0\n\tSFPCOMPC\n\tSFPMOV	L0, L2, 2\n\tSFPMOV	L0,L9,8\n\tSFPENCC	3, 10\n\tret\n} } }

void baz () {
  // Do not CSE
  vInt r = rand () + rand();
  l_reg[LRegs::LReg0] = r;
}
// { dg-final { scan-assembler {\n_Z3bazv:\n\tSFPMOV	L1,L9,8\n\tSFPMOV	L0,L9,8\n\tSFPIADD	L0, L1, 0, 4\n\tret\n} } }
