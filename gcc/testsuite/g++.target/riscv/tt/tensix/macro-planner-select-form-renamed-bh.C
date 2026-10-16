// Check that a renamed mixed-mode select refuses cc-restore-store-race (the Dst-ownership fold is disabled so it reaches that gate).
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -mno-tt-tensix-optimize-dst-ownership -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump "Macro-planner schedule-candidate: cc-compact" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner descriptor-refusal: cc-restore-store-race" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "Macro-planner schedule-refusal: cc-separator-kept-silicon-unproven" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "Macro-planner formed" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-not "\\.ttinsn" } }
// { dg-final { scan-assembler-not "SFPCONFIG" } }
// { dg-final { scan-assembler-times "SFPSETCC" 1 } }
// { dg-final { scan-assembler-times "TTINCRWC" 8 } }

#define PICK()                                                                \
  do                                                                          \
    {                                                                         \
      __builtin_rvtt_sfppushc (0);                                            \
      __builtin_rvtt_sfppopc (0);                                             \
      auto gate = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 2, 7);            \
      auto lhs = __builtin_rvtt_sfpload (nullptr, 0, 0, 32, 6, 7);            \
      auto rhs = __builtin_rvtt_sfpload (nullptr, 0, 0, 64, 6, 7);            \
      __builtin_rvtt_sfppushc (0);                                            \
      __builtin_rvtt_sfpsetcc (gate, 2);                                    \
      auto chosen = __builtin_rvtt_sfpassign_lv (rhs, lhs);                   \
      __builtin_rvtt_sfppopc (0);                                             \
      __builtin_rvtt_sfpstore (nullptr, chosen, 0, 0, 0, 6, 7);               \
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);                                   \
    }                                                                         \
  while (0)

__attribute__((noinline)) void choose_lanes ()
{
  PICK ();
  PICK ();
  PICK ();
  PICK ();
  PICK ();
  PICK ();
  PICK ();
  PICK ();
}
