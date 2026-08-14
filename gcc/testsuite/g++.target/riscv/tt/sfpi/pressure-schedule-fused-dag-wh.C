// { dg-do compile }
// { dg-options "-mcpu=tt-wh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-pressure-schedule -fdump-tree-rvtt_lp_schedule" }
// { dg-final { scan-tree-dump "SFPU pressure schedule:.*old-peak=9.*new-peak=8.*validated=yes.*reason=ok.*applied=yes" "rvtt_lp_schedule" } }

namespace ckernel {
unsigned *instrn_buffer;
}

#include <sfpi.h>

using namespace sfpi;

void
test ()
{
  vFloat a0 = l_reg[LRegs::LReg0];
  vFloat a1 = l_reg[LRegs::LReg1];
  vFloat a2 = l_reg[LRegs::LReg2];
  vFloat a3 = l_reg[LRegs::LReg3];
  vFloat a4 = l_reg[LRegs::LReg4];
  vFloat a5 = l_reg[LRegs::LReg5];
  vFloat a6 = l_reg[LRegs::LReg6];
  vFloat a7 = l_reg[LRegs::LReg7];

#ifdef MANUAL_EARLY_FOLD
  vFloat folded = a6 + a7;
#endif

  /* A0 and A1 remain live for REUSED, so defining PARTIAL first raises the
     source-order live set from eight to nine.  Folding A6/A7 first kills two
     inputs and creates one value, leaving room for the rest of this generic
     fused arithmetic DAG.  */
  vFloat partial = a0 + a1;
#ifndef MANUAL_EARLY_FOLD
  vFloat folded = a6 + a7;
#endif
  vFloat reused = a0 * a1;
  vFloat left = partial * a2 + a3;
  vFloat right = reused * a4 + a5;
  vFloat result = left * right + folded;

  l_reg[LRegs::LReg0] = result;
}

