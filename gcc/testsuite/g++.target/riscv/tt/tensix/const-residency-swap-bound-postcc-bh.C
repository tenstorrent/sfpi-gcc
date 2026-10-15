// PRESSURE-PARK post-CC admission of an SFPSWAP bound.  SFPSWAP writes
// both operands back but drops the write to a constant register, so a
// loop-invariant bound in VC whose VC result is unused can be read from
// a PRGM register in place instead of being reloaded every iteration.
// A bound in VD (a VD of L12 or above suppresses the swap) and a bound
// whose VC result is consumed both stay in the loop.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-pressure-park -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-times "pressure-park: admitted post-CC candidate" 1 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "pressure-park: refused .consumer-lane-discipline-unaudited.: r_\\d+ = __builtin_rvtt_sfpswap" 2 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "allocated PRGM L1\\d for constant 0x40a00000 .loop class" 1 "rvtt_prgm_const" } }
// { dg-final { scan-assembler-times "SFPSWAP\tL\[0-7\], L1\[2-4\], 1" 1 } }

void swap_bound_vc_fire (void)
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  __builtin_rvtt_sfppushc (0);
  __builtin_rvtt_sfpsetcc (x, 0);
  __builtin_rvtt_sfppopc (0);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc (x, 0);
      __builtin_rvtt_sfppopc (0);
      auto bound = __builtin_rvtt_sfpxloadi (nullptr, 0x40a00000, 0, 0, 31);
      auto r = __builtin_rvtt_sfpswap (x, bound, 1);
      x = __builtin_rvtt_sfpselect2 (r, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}

void swap_bound_vd_refuse (void)
{
  auto x = __builtin_rvtt_sfpreadlreg (1);
  __builtin_rvtt_sfppushc (0);
  __builtin_rvtt_sfpsetcc (x, 0);
  __builtin_rvtt_sfppopc (0);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc (x, 0);
      __builtin_rvtt_sfppopc (0);
      auto bound = __builtin_rvtt_sfpxloadi (nullptr, 0x40c00000, 0, 0, 31);
      auto r = __builtin_rvtt_sfpswap (bound, x, 1);
      x = __builtin_rvtt_sfpselect2 (r, 1);
    }
  __builtin_rvtt_sfpwritelreg (x, 1);
}

void swap_bound_both_halves_refuse (void)
{
  auto x = __builtin_rvtt_sfpreadlreg (2);
  auto y = __builtin_rvtt_sfpreadlreg (3);
  __builtin_rvtt_sfppushc (0);
  __builtin_rvtt_sfpsetcc (x, 0);
  __builtin_rvtt_sfppopc (0);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc (x, 0);
      __builtin_rvtt_sfppopc (0);
      auto bound = __builtin_rvtt_sfpxloadi (nullptr, 0x40e00000, 0, 0, 31);
      auto r = __builtin_rvtt_sfpswap (x, bound, 1);
      x = __builtin_rvtt_sfpselect2 (r, 0);
      y = __builtin_rvtt_sfpadd (y, __builtin_rvtt_sfpselect2 (r, 1), 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 2);
  __builtin_rvtt_sfpwritelreg (y, 3);
}
