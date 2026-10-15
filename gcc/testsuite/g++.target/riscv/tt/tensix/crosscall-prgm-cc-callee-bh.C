// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -fno-exceptions -fno-rtti -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-crosscall-hoist -mtt-tensix-optimize-crosscall-prgm -fdump-tree-rvtt_prgm_const-details" }
// The callee's rows carry a structured v_if region (push/setcc/pop):
// CC writes cannot reach a programmable constant register, and the
// moved programming runs before the callee under the caller's
// all-lanes state, so the move still fires.
// { dg-final { scan-tree-dump "crosscall-prgm: moved 1 programming pair" "rvtt_prgm_const" } }
// { dg-final { scan-assembler-times "SFPCONFIG" 1 } }

__attribute__((noinline)) void
cpc_callee ()
{
  for (int row = 0; row != 8; ++row)
    {
      auto x = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);
      auto gain = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b1a3d, 0, 0, 31);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc (x, 0);
      x = __builtin_rvtt_sfpmul (x, gain, 0);
      __builtin_rvtt_sfppopc (0);
      __builtin_rvtt_sfpstore (nullptr, x, 0, 0, 0, 6, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}

void
cpc_caller (int tiles)
{
  for (int t = 0; t != tiles; ++t)
    cpc_callee ();
}
