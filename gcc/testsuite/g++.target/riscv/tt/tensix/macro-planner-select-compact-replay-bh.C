// Compact-calendar delivery increment (a): under the opt-in
// -mtt-tensix-macro-planner-replay flag, planner-formed SFPLOADMACRO
// launches are admitted into automatic replay recording, so the
// compact loop body's eight identical 3-word rows record once
// (execute-while-record) and replay -- the handwritten Where
// protocol's own delivery shape.  Body per trip: one recording launch,
// six recorded words (two rows), three replays (six rows), and the
// drain.  Without the flag the calendar stays RISC-pushed
// byte-identically (macro-planner-select-compact-loop-bh.C).
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -mtt-tensix-macro-planner-replay -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump "Macro-planner schedule-candidate: cc-compact" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner formed: rows=8 runs=1 config=preheader lane-proof=materialized-enable" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-times "TTREPLAY\\t0, 6, 1, 1" 1 } }
// { dg-final { scan-assembler-times "TTREPLAY\\t0, 6, 0, 0" 3 } }
// Four launch words remain RISC-pushed (the recorded two rows), plus
// the three owned SETC16 words in the preheader prefix.
// { dg-final { scan-assembler-times "\\.ttinsn" 7 } }
// { dg-final { scan-assembler-times "SFPLOAD\\tL0, 64, 6, 6" 2 } }
// { dg-final { scan-assembler-not "TTINCRWC" } }
// { dg-final { scan-assembler-times "SFPENCC" 1 } }

#define ROW()                                                                 \
  do                                                                          \
    {                                                                         \
      auto condition = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);       \
      auto on_true = __builtin_rvtt_sfpload (nullptr, 32, 0, 0, 6, 7);        \
      auto on_false = __builtin_rvtt_sfpload (nullptr, 64, 0, 0, 6, 7);       \
      __builtin_rvtt_sfppushc (0);                                            \
      __builtin_rvtt_sfpsetcc_v (condition, 2);                               \
      auto result = __builtin_rvtt_sfpassign_lv (on_false, on_true);          \
      __builtin_rvtt_sfppopc (0);                                             \
      __builtin_rvtt_sfpstore (nullptr, result, 0, 0, 0, 6, 7);               \
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);                                   \
    }                                                                         \
  while (0)

__attribute__((noinline)) void select_faces_compact_replay (unsigned faces)
{
  unsigned face = 0;
  do
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
  while (++face < faces);
}
