// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -fno-exceptions -fno-rtti -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-crosscall-hoist -mtt-tensix-optimize-crosscall-prgm -fdump-tree-rvtt_prgm_const-details" }
// The caller's tile loop carries its own vector dataflow: its register
// allocation is invisible to the proof, so the move refuses and the
// callee keeps its per-call programming.
// { dg-final { scan-tree-dump "refused .crosscall-caller-stmt-unproven." "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "crosscall-prgm: moved" "rvtt_prgm_const" } }

__attribute__((noinline)) void
cpv_callee ()
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
cpv_caller (int tiles)
{
  for (int t = 0; t != tiles; ++t)
    {
      auto y = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);
      y = __builtin_rvtt_sfpabs (y, 1);
      __builtin_rvtt_sfpstore (nullptr, y, 0, 0, 0, 6, 7);
      cpv_callee ();
    }
}
