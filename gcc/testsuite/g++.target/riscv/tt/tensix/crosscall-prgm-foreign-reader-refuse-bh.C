// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -fno-exceptions -fno-rtti -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-crosscall-hoist -mtt-tensix-optimize-crosscall-prgm -fdump-tree-rvtt_prgm_const-details" }
// Zero-trip observability: the caller reads the residency register
// after the loop.  The moved programming would write it on loop entry
// even when no call runs, so the move refuses.
// { dg-final { scan-tree-dump "refused .crosscall-caller-foreign-contract." "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "crosscall-prgm: moved" "rvtt_prgm_const" } }

__attribute__((noinline)) void
cpr_callee ()
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
cpr_caller (int tiles)
{
  for (int t = 0; t != tiles; ++t)
    cpr_callee ();
  auto g = __builtin_rvtt_sfpreadlreg (12);
  __builtin_rvtt_sfpstore (nullptr, g, 0, 0, 0, 6, 7);
}
