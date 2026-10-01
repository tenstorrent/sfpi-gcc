// Compact-calendar near-miss: a uniform-mode select whose typed Dst stride is 4.
// The tables' address-modifier machinery proves only the Dst += 2
// program, so the compact candidate refuses by name; the established
// 4-slot calendar it falls back to derives the same racing slots as
// every kept-separator select (restore exec == store exec == 3) and,
// since the Where hardware adjudication was root-caused
// (the reference simulator, live store lane mask), its descriptor refuses
// cc-restore-store-race -- the refusal keys on the derived slots and
// proven delays, not on the stride value, the misc word, or the (here
// uniform) data modes.  Every byte stays on the semantic (planner-OFF)
// lowering.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump "Macro-planner schedule-candidate: cc-compact" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner schedule-refusal: cc-template-unproved" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner descriptor-refusal: cc-restore-store-race" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "Macro-planner descriptor-cc:.*separator=kept" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "Macro-planner formed" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-not "\\.ttinsn" } }
// { dg-final { scan-assembler-not "SFPCONFIG" } }
// { dg-final { scan-assembler-times "SFPSETCC" 1 } }
// { dg-final { scan-assembler-times "TTINCRWC" 8 } }

#define SELECT_ADDR_MODE 7
#define SELECT_COND_MODE 6
#define SELECT_STRIDE 4
#include "macro-planner-select-body.h"

__attribute__((noinline)) void select_stride_four ()
{
  __builtin_rvtt_sfppushc (0);
  __builtin_rvtt_sfppopc (0);
  SELECT_ROWS_8 ();
}
