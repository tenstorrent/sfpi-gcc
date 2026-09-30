// Near miss for the LaneConfig default-reset row: dest 15 and mod1
// bit0 set but imm16 != 0 (word 0x910100F1 -- imm16 = 0x0001 can set
// LaneConfig behavior bits, the lane model is unproven).  Stays
// refused by class; byte-identical refusal, no allocation.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-prgm-const -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump "prgm-const: refused .opaque-region-undeclared.: raw SFPCONFIG writes LaneConfig" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "prgm-const: allocated" "rvtt_prgm_const" } }
// { dg-final { scan-assembler-not "SFPCONFIG" } }
// { dg-final { scan-assembler "SFPADDI" } }

void lane_write_nonzero_imm ()
{
  __asm__ __volatile__ (".ttinsn %0" : : "n" (0x910100F1));
}

void prgm_const_blocked_by_lane_write ()
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  auto s = __builtin_rvtt_sfpreadlreg (1);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto prod = __builtin_rvtt_sfpmul (x, s, 0);
      x = __builtin_rvtt_sfpaddi (nullptr, prod, 0x42fe, 0, 0, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}
