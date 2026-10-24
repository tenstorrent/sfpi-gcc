// A zero-effect lifetime marker may be the first executable RTL in an arm.
// Its predecessor in the function-wide chain can be a BARRIER belonging to
// the other arm.  BARRIER has no BLOCK_FOR_INSN field: neither marker lookup
// nor the scalar lane-proof walks may inspect that field across the boundary.
// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fdump-rtl-expand" }
// { dg-final { scan-rtl-dump {\(barrier} "expand" } }
// { dg-final { scan-assembler-times "# RAWLREG_EFFECT 1, 0" 1 } }
// { dg-final { scan-assembler-times "# RAWLREG_EFFECT 0, 0" 1 } }

void
raw_marker_first_in_arm (bool take_read)
{
  if (take_read)
    __builtin_rvtt_sfprawlreg_effect (1, 0);
  else
    __builtin_rvtt_sfprawlreg_effect (0, 0);
}
