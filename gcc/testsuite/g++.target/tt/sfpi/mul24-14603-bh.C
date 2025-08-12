// { dg-options "-mcpu=tt-bh -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }

namespace ckernel{
    unsigned *instrn_buffer;
}
#include <sfpi.h>

using namespace sfpi;

void foo () {
  // Basic test
  vFloat a = l_reg[LRegs::LReg0];
  vFloat b = l_reg[LRegs::LReg1];
  vFloat r_lo = __builtin_rvtt_sfpmul24 (a.get(), b.get(), SFPMUL24_MOD1_LOWER);
  vFloat r_hi = __builtin_rvtt_sfpmul24 (a.get(), b.get(), SFPMUL24_MOD1_UPPER);
  l_reg[LRegs::LReg2] = r_lo;
  l_reg[LRegs::LReg3] = r_hi;
}
// { dg-final { scan-assembler {\n_Z3foov:\n\tSFPMUL24	L2, L0, L1, 0\n\tSFPMUL24	L3, L0, L1, 1\n\tret\n} } }

void bar () {
  // Live value test
  vFloat a = l_reg[LRegs::LReg0];
  vFloat b = l_reg[LRegs::LReg1];
  vFloat c = l_reg[LRegs::LReg4];
  vFloat d = l_reg[LRegs::LReg5];
  vFloat r_lo, r_hi;
  
  v_if (a < b) {
    r_lo = c;
    r_hi = d;
  } v_else {
    r_lo = __builtin_rvtt_sfpmul24 (a.get(), b.get(), SFPMUL24_MOD1_LOWER);
    r_hi = __builtin_rvtt_sfpmul24 (a.get(), b.get(), SFPMUL24_MOD1_UPPER);
  } v_endif;

  l_reg[LRegs::LReg2] = r_lo;
  l_reg[LRegs::LReg3] = r_hi;
}
// { dg-final { scan-assembler {\n_Z3barv:\n\tSFPMAD	L2, L1, L11, L0, 0\n\tSFPNOP\n\tSFPSETCC	L2, 0, 0\n\tSFPCOMPC\n\tSFPMOV	L2, L4, 2\n\tSFPMUL24	L2, L0, L1, 0\n\tSFPMOV	L3, L5, 2\n\tSFPMUL24	L3, L0, L1, 1\n\tSFPENCC	3, 10\n\tret\n} } }

