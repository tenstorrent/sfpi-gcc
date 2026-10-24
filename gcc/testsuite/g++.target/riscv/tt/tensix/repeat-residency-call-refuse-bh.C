// Check that a call in the body refuses the repeat residency class.
// { dg-options "-mcpu=tt-bh-tensix -O2 -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-repeat-residency -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump "const-residency: repeat class refused \\(the function has a call or asm\\)" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "repeat group" "rvtt_prgm_const" } }

extern void other_tile (void);

__attribute__((noinline)) void repeat_rows_with_call (void)
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
#pragma GCC unroll 8
  for (unsigned ix = 0; ix != 8; ++ix)
    {
      auto gain = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b1a3d, 0, 0, 31);
      x = __builtin_rvtt_sfpmul (x, gain, 0);
      if (ix == 3)
	other_tile ();
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}
