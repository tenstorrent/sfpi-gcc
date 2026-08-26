// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-crosscall-hoist -mtt-tensix-optimize-crosscall-config-prefix -fdump-tree-rvtt_crosscall" }
// Residency walk STOP: the batch loop carries a CC-writing statement
// between tile loops -- lifting the lane-predicated loads across it is
// unproven; the walk stops (a stop is not a refusal) and the contract
// commits at the tile loop's own entry.
// { dg-final { scan-tree-dump "residency walk stops at loop" "rvtt_crosscall" } }
// { dg-final { scan-tree-dump-not "contract placement lifted" "rvtt_crosscall" } }
// { dg-final { scan-tree-dump "hoisted 6 contract materializations ..config prefix. from .* into 1 caller" "rvtt_crosscall" } }

__attribute__((noinline)) void
ccs_callee ()
{
  auto v = __builtin_rvtt_sfploadi (nullptr, 16128, 0, 0, 0);
  __builtin_rvtt_sfpwriteconfig_v (v, 12);
  auto a0 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4ccccd, 0, 0, -32);
  auto a1 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e87ae14, 0, 0, -32);
  auto a2 = __builtin_rvtt_sfpxloadi (nullptr, 0x3dbba5e3, 0, 0, -32);
  auto b0 = __builtin_rvtt_sfpxloadi (nullptr, 0x3ea3d70a, 0, 0, -32);
  auto b1 = __builtin_rvtt_sfpxloadi (nullptr, 0xbd23d70a, 0, 0, -32);
  auto b2 = __builtin_rvtt_sfpxloadi (nullptr, 0x3f2e147b, 0, 0, -32);
  for (int row = 0; row != 8; ++row)
    {
      auto x = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);
      auto r = __builtin_rvtt_sfplutfp32_6r (a0, a1, a2, b0, b1, b2, x, 4);
      auto c = __builtin_rvtt_sfpreadlreg (12);
      r = __builtin_rvtt_sfpmul (r, c, 0);
      __builtin_rvtt_sfpstore (nullptr, r, 0, 0, 0, 6, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}

void
ccs_caller (int batches, int tiles)
{
  for (int b = 0; b != batches; ++b)
    {
      for (int t = 0; t != tiles; ++t)
	ccs_callee ();
      __builtin_rvtt_sfpencc (0, 10);	/* CC writer in the batch loop */
    }
}
