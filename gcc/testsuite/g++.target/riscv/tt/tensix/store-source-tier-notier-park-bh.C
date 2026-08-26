// STORE-SOURCE TIER without the LREG tier (lane HO): the knob is on
// but -mtt-tensix-optimize-pressure-park is not, so the tier has no
// hoist to offer.  The candidate falls through and KEEPS the
// established parked placement (SFPCONFIG programs the PRGM register;
// the per-row SFPMOV copy remains): the one-word-per-row copy never
// loses to the two-word-per-row in-loop rematerialization a bare
// refusal would leave behind.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-store-source-tier -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-times "store-source-tier .store-source-encoding-ceiling." 1 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "pressure-park: hoisted" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "allocated PRGM L1\\d for constant 0x3e4b1a3d .loop class" 1 "rvtt_prgm_const" } }
// { dg-final { scan-assembler "SFPCONFIG" } }
// { dg-final { scan-assembler "SFPMOV" } }

void tier_notier_park (void)
{
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto fill = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b1a3d, 0, 0, 31);
      __builtin_rvtt_sfpstore (nullptr, fill, 0, 0, 0, 0, 0);
    }
}
