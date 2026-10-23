#include "rtl-lane-reset.h"

// Check that the compact select keeps the restore launch and explicit payload load directly behind the definition launch.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -mtt-tensix-macro-planner-replay -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump "Macro-planner descriptor-cc: sense=complement def-visible=2 pre-load=1 post-load=2 store-exec=3 restore-visible=3 interval=3 separator=absorbed" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner descriptor-word dest=4: 0x13000004" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner descriptor-word dest=5: 0x00000005" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner descriptor-word dest=8: 0x00000770" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-times "TTREPLAY\\t0, 6, 1, 1" 1 } }
// The definition launch (0x9306e000), the restore launch (0x9346e020) and
// the explicit payload load, in consecutive slots, for both recorded rows.
// { dg-final { scan-assembler-times "\\.ttinsn\\t2466701312\\n\\t\\.ttinsn\\t2470895648\\n\\tSFPLOAD\\tL0, 64, 6, 6\\n" 2 } }

#define ROW()                                                                 \
  do                                                                          \
    {                                                                         \
      auto condition = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);       \
      auto on_true = __builtin_rvtt_sfpload (nullptr, 32, 0, 0, 6, 7);        \
      auto on_false = __builtin_rvtt_sfpload (nullptr, 64, 0, 0, 6, 7);       \
      __builtin_rvtt_sfppushc (0);                                            \
      __builtin_rvtt_sfpsetcc (condition, 2);                                 \
      auto result = __builtin_rvtt_sfpassign_lv (on_false, on_true);          \
      __builtin_rvtt_sfppopc (0);                                             \
      __builtin_rvtt_sfpstore (nullptr, result, 0, 0, 0, 6, 7);               \
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);                                   \
    }                                                                         \
  while (0)

__attribute__((noinline)) void select_compact_cc_slots (unsigned faces)
{
  RVTT_TEST_LANE_RESET ();
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
