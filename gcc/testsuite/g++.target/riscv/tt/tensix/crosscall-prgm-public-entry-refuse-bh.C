// A public callee may be entered without the private caller's lane setup.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -fno-exceptions -fno-rtti -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-crosscall-hoist -mtt-tensix-optimize-crosscall-prgm -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-not "crosscall-prgm: moved" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "allocated PRGM" "rvtt_prgm_const" } }
// Only the explicit caller LaneConfig reset, not callee programming, is issued.
// { dg-final { scan-assembler-times "SFPCONFIG" 1 } }

__attribute__((noinline)) void
public_entry (void)
{
  for (unsigned row = 0; row != 8; ++row)
    {
      auto x = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);
      auto gain = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b1a3d, 0, 0, 31);
      x = __builtin_rvtt_sfpmul (x, gain, 0);
      __builtin_rvtt_sfpstore (nullptr, x, 0, 0, 0, 6, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}

void caller_with_known_lanes (unsigned tiles)
{
  __builtin_rvtt_sfpencc_all_lanes ();
  __builtin_rvtt_sfpconfig_i (0, 15, 1);
  for (unsigned tile = 0; tile != tiles; ++tile)
    public_entry ();
}
