// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-crosscall-hoist -mtt-tensix-optimize-crosscall-config-prefix -fdump-tree-rvtt_crosscall" }
// Residency walk FIRE: the tile loop sits inside a scalar batch loop
// whose extra body statements pass the same caller-epoch scan; the
// contract (pair + coefficients) programs at the BATCH loop's entry --
// once per kernel, the hand init discipline.
// { dg-final { scan-tree-dump "contract placement lifted to enclosing loop" "rvtt_crosscall" } }
// { dg-final { scan-tree-dump "hoisted 6 contract materializations ..config prefix. from .* into 1 caller" "rvtt_crosscall" } }

__attribute__((noinline)) void
ccn_callee ()
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

int ccn_pace;

void
ccn_caller (int batches, int tiles)
{
  for (int b = 0; b != batches; ++b)
    {
      ccn_pace = b;			/* scalar batch bookkeeping */
      for (int t = 0; t != tiles; ++t)
	ccn_callee ();
    }
}
