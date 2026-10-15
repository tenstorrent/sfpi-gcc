// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -fno-exceptions -fno-rtti -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-crosscall-hoist -fdump-tree-rvtt_prgm_const-details" }
// Default-off twin of crosscall-prgm-fire-bh.C: without the flag the
// callee keeps programming its residency register on every call.
// { dg-final { scan-tree-dump "allocated PRGM L12 for constant" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "crosscall-prgm" "rvtt_prgm_const" } }
// { dg-final { scan-assembler-times "SFPCONFIG" 1 } }

__attribute__((noinline)) void
cpo_callee ()
{
  for (int row = 0; row != 8; ++row)
    {
      auto x = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);
      auto gain = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b1a3d, 0, 0, 31);
      x = __builtin_rvtt_sfpmul (x, gain, 0);
      __builtin_rvtt_sfpstore (nullptr, x, 0, 0, 0, 6, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}

void
cpo_caller (int tiles)
{
  for (int t = 0; t != tiles; ++t)
    cpo_callee ();
}
