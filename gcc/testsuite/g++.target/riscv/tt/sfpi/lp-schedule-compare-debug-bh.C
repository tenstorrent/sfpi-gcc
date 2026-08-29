// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fcompare-debug -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-pressure-schedule -mtt-tensix-pressure-schedule-use-milp" }
// The lp-schedule-milp-fire body under -fcompare-debug (audit finding
// IP-6): the driver compiles twice — with and without debug binds in
// the IL — and hard-errors unless the final code is IDENTICAL.  This is
// the debug-transparency contract itself: the scheduling decision (a
// firing one — the milp-fire twin proves this exact body applies) may
// not depend on the debug level.  The pass's original
// debug_info_level == DINFO_LEVEL_NONE gate would FAIL this test.

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

  vFloat t0 = a0 * a2 + a5;
  vFloat t1 = a2 + a3;
  vFloat t2 = a6 + a7;
  vFloat t3 = t2 * a7 + t0;
  vFloat t4 = t0 + t1;
  vFloat t5 = a2 + a7;
  vFloat t6 = a3 * t4 + t0;
  vFloat t7 = t3 + a4;
  vFloat t8 = t4 + t7;
  vFloat t9 = t4 * a2 + t5;
  vFloat t10 = a1 + a7;
  vFloat t11 = t7 * a6 + t6;
  vFloat t12 = t11 + t10;

  l_reg[LRegs::LReg0] = t8;
  l_reg[LRegs::LReg1] = t9;
  l_reg[LRegs::LReg2] = t12;
}
