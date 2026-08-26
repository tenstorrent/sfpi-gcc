// STORE-SOURCE TIER knob-off control -- the HL-F1 copy tax made
// visible (lane HO): the exact store-source-tier-fire-bh.C first body
// WITHOUT -mtt-tensix-optimize-store-source-tier.  The established
// residency placement parks the store-consumed constant in a PRGM
// register (L12-L14); SFPSTORE sources L0-L11 only, so the register
// allocator materializes the per-row SFPMOV copy out of the constant
// file -- the word the tier exists to erase.  Byte-identity of the
// knob-off world is the corpus gate's job; this twin pins the tax
// itself in the assembler.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-pressure-park -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-not "store-source-tier" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "allocated PRGM L1\\d for constant 0x3e4b1a3d .loop class" 1 "rvtt_prgm_const" } }
// { dg-final { scan-assembler "SFPCONFIG" } }
// { dg-final { scan-assembler "SFPMOV" } }

void tier_off_tax (void)
{
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto fill = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b1a3d, 0, 0, 31);
      __builtin_rvtt_sfpstore (nullptr, fill, 0, 0, 0, 0, 0);
    }
}
