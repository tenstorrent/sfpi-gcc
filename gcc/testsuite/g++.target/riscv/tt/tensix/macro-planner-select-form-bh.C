// The CC-template extension, REFUSING by the ARCHITECTURAL name since
// the Where hardware adjudication (Blackhole device,
// verdicts/VERDICT.md) was root-caused by the corrected reference-simulator delivery
// model (the reference simulator: the store's lane predicate is LIVE at
// execution, never launch-latched): the predicated-select (TTNN Where
// class) at MIXED modes (condition mode 2, payload/store mode 6).  The
// compact 3-slot candidate is tried first and its descriptor refuses
// by name -- launch-sourced store mod0 obliges the definition
// carrier's mode to equal the store mode -- and the established 4-slot
// separator-kept calendar reaches descriptor synthesis, where the CC
// model derives its slots (store exec = 0+1+2 = 3, restore exec =
// 2+1+0 = 3: the all-lanes restore retires in the store's own cycle,
// so the store executes under the SFPSETCC complement mask) and
// refuses cc-restore-store-race.  Every candidate refuses, so the
// region refuses byte-identically: the semantic (planner-OFF) lowering
// below is the hardware-green form.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump "Macro-planner region: rows=8 row-len=7 runs=1 stride=2" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner schedule-candidate: cc-compact" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner descriptor-refusal: cc-template-unproved" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner schedule: ii=4 issues=4 launches=2 explicit=2 launched-events=3" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner descriptor-refusal: cc-restore-store-race" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "Macro-planner schedule-refusal: cc-separator-kept-silicon-unproven" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "Macro-planner descriptor-cc:.*separator=kept" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "Macro-planner formed" "rvtt_macro_planner" } }
// Refusal paths never mutate: every byte stays the explicit semantic
// lowering -- the predicate writes, merges, restores, stores, and the
// kept typed separators all survive; no descriptor is configured.
// { dg-final { scan-assembler-not "\\.ttinsn" } }
// { dg-final { scan-assembler-not "SFPCONFIG" } }
// { dg-final { scan-assembler-not "SFPLOADMACRO" } }
// { dg-final { scan-assembler-times "SFPSETCC" 1 } }
// { dg-final { scan-assembler-times "SFPSTORE" 1 } }
// { dg-final { scan-assembler-times "TTINCRWC" 8 } }

#define SELECT_ADDR_MODE 7
#include "macro-planner-select-body.h"

__attribute__((noinline)) void select_rows ()
{
  SELECT_ROWS_8 ();
}
