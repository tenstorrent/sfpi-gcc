// Check that macro-planner launches of a CC-writing calendar get no emission record, so the replay hoist still refuses them as effect-opaque.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-unroll-loops -mtt-tensix-macro-planner -mtt-tensix-macro-planner-replay -mtt-tensix-optimize-replay-hoist -fdump-rtl-rvtt_replay-details -fdump-rtl-rvtt_macro_planner" }
// { dg-final { scan-rtl-dump "reissue-unproved: payload insn \\d+ is effect-opaque" "rvtt_replay" } }
// { dg-final { scan-rtl-dump "Not hoisting: replay-reissue-latency-unproved" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "planner-derived launch effects" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "Hoisted no-exec capture" "rvtt_replay" } }
// { dg-final { scan-rtl-dump "Macro-planner formed: rows=8" "rvtt_macro_planner" } }

#define ROW()                                                                 \
  do                                                                          \
    {                                                                         \
      auto condition = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);       \
      auto on_true = __builtin_rvtt_sfpload (nullptr, 32, 0, 0, 6, 7);        \
      auto on_false = __builtin_rvtt_sfpload (nullptr, 64, 0, 0, 6, 7);       \
      __builtin_rvtt_sfppushc (0);                                            \
      __builtin_rvtt_sfpsetcc (condition, 2);                               \
      auto result = __builtin_rvtt_sfpassign_lv (on_false, on_true);          \
      __builtin_rvtt_sfppopc (0);                                             \
      __builtin_rvtt_sfpstore (nullptr, result, 0, 0, 0, 6, 7);               \
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);                                   \
    }                                                                         \
  while (0)

__attribute__((noinline)) void select_loop_counted ()
{
  for (unsigned face = 0; face != 8; ++face)
    {
      ROW ();
      ROW ();
      ROW ();
      ROW ();
      ROW ();
      ROW ();
      ROW ();
      ROW ();
    }
}
