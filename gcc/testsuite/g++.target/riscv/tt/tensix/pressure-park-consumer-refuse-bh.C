// PRESSURE-PARK near miss, consumer audit (lane GV): the post-CC
// candidate's value is consumed by a cross-lane SFPSHFT2 subvector
// shuffle, which reads lanes of its operand OTHER than the one it
// writes -- including lanes the original predicated SFPLOADI left
// indeterminate -- so the
// constant-register substitution is not covered by the lane-predicated
// consumer audit and the candidate refuses by name.  Nothing is
// programmed, nothing is peeled, and no LREG hoist happens either (the
// LREG tier only sees candidates that passed admission).
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-pressure-park -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump "pressure-park: refused .consumer-lane-discipline-unaudited." "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "pressure-park: admitted post-CC candidate" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "allocated PRGM" "rvtt_prgm_const" } }
// { dg-final { scan-assembler-not "SFPCONFIG" } }

void park_consumer_refuse (void)
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc_v (x, 0);
      __builtin_rvtt_sfppopc (0);
      auto gain = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b1a3d, 0, 0, 31);
      auto shuf = __builtin_rvtt_sfpshft2_subvec_shfl1 (gain, 3);
      x = __builtin_rvtt_sfpmul (x, shuf, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}
