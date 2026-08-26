// PRESSURE-PARK fire, post-CC position admission (lane GV): the
// invariant materialization sits AFTER the body's first CC writer --
// exactly the shape the peel class's position rule refuses today
// (const-residency-peel-position-refuse-bh.C is this body without the
// knob).  Under -mtt-tensix-optimize-pressure-park the candidate is
// admitted because its only consumer (SFPMUL) is in the audited
// lane-predicated set: the constant-register read carries the constant
// in EVERY lane, a strict superset of the lanes the original
// predicated SFPLOADI wrote, so all originally-defined lanes are
// bit-exact and only originally-indeterminate lanes change (the
// invariant pass's ratified superset-write refinement).  The first
// iteration is peeled and the programming follows the peeled all-lanes
// SFPENCC.  The twin varies names, the constant, and the trip count.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-pressure-park -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-times "pressure-park: admitted post-CC candidate" 2 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "peeled first iteration" 2 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "allocated PRGM L1\\d for constant 0x\[0-9a-f\]+ .loop class" 2 "rvtt_prgm_const" } }
// { dg-final { scan-assembler-times "SFPCONFIG" 2 } }

void park_postcc_fire (void)
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc_v (x, 0);
      __builtin_rvtt_sfppopc (0);
      auto gain = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b1a3d, 0, 0, 31);
      x = __builtin_rvtt_sfpmul (x, gain, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}

void renamed_varied_scale (void)
{
  auto west = __builtin_rvtt_sfpreadlreg (2);
  for (unsigned step = 0; step != 12; ++step)
    {
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc_v (west, 0);
      __builtin_rvtt_sfppopc (0);
      auto bias = __builtin_rvtt_sfpxloadi (nullptr, 0x40e90fdb, 0, 0, 31);
      west = __builtin_rvtt_sfpmul (west, bias, 0);
    }
  __builtin_rvtt_sfpwritelreg (west, 2);
}
