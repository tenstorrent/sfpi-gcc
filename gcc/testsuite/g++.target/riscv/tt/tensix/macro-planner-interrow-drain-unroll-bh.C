// Inter-row drain composition twin: the exact weekly-RED shape -- a counted
// signbit-class row loop granted the replay-loop-unroll request, then
// formed by the planner as an 8-row region on the fixed-VD whole-word
// program.  The unfixed emission dropped the per-row drains (8
// back-to-back launches, one trailing drain: device corr FAIL, weekly
// end to end on hardware, simulator-reproduced); with the inter-row
// drain every launch again carries its derived drain.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-optimize-dst-autoincr -mtt-tensix-optimize-replay-loop-unroll -fdump-tree-rvtt_replay_unroll -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-tree-dump "requested unroll 8 of loop" "rvtt_replay_unroll" } }
// { dg-final { scan-rtl-dump-times "Macro-planner drain-interrow: drain=3 rows=8" 1 "rvtt_macro_planner" } }
// { dg-final { scan-assembler-times "SFPNOP" 24 } }
// { dg-final { scan-assembler-not "SFPSHFT" } }
// { dg-final { scan-assembler-not "SFPCAST" } }

__attribute__((noinline)) void signbit_row_loop ()
{
  for (int row = 0; row < 32; ++row)
    {
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfppopc (0);
      auto loaded = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      auto shifted = __builtin_rvtt_sfpshft_i (nullptr, loaded, -31, 0, 0, 0);
      auto converted = __builtin_rvtt_sfpcast (shifted, 0);
      __builtin_rvtt_sfpstore (nullptr, converted, 0, 0, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
