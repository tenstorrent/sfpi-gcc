// STORE-SOURCE TIER vs the store-sink license token: with
// BOTH -mtt-tensix-optimize-store-sink and the tier knob on, the
// license's own stricter place() refusal keeps precedence exactly as
// it first shipped (dump line "refused (store-source-encoding-
// ceiling)", every candidate class, no park fallback) and the
// loop-class fallback still reaches the LREG tier -- the licensed
// word accounting is unchanged by the general knob.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-pressure-park -mtt-tensix-optimize-store-sink -mtt-tensix-optimize-store-source-tier -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-times "const-residency: refused .store-source-encoding-ceiling." 1 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "store-source-tier .store-source-encoding-ceiling." "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "pressure-park: hoisted invariant materialization to a free LREG" 1 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "allocated PRGM" "rvtt_prgm_const" } }
// { dg-final { scan-assembler-not "SFPCONFIG" } }

void tier_license_precedence (void)
{
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto fill = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b1a3d, 0, 0, 31);
      __builtin_rvtt_sfpstore (nullptr, fill, 0, 0, 0, 0, 0);
    }
}
