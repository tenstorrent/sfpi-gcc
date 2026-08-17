// Wormhole twin of macro-planner-select-form-bh.C, REFUSING by the
// architectural name since the 2026-08-17 Where silicon adjudication
// (evidence root ~/sfpi-uplift/where-adjudication-20260817; root cause
// craq-sim 9f324140 -- live store lane mask): the mixed-mode select's
// compact candidate refuses by name and the established 4-slot
// separator-kept calendar derives restore exec == store exec == 3, so
// its descriptor refuses cc-restore-store-race (the silicon evidence
// is BH; the calendar, its proven delays, and the delivery model are
// the same table rows on WH -- the same race on both).  The region
// refuses byte-identically to the semantic (planner-OFF) lowering.
// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump "Macro-planner schedule-candidate: cc-compact" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner descriptor-refusal: cc-template-unproved" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner descriptor-refusal: cc-restore-store-race" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "Macro-planner schedule-refusal: cc-separator-kept-silicon-unproven" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "Macro-planner descriptor-cc:.*separator=kept" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "Macro-planner formed" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-not "\\.ttinsn" } }
// { dg-final { scan-assembler-not "SFPCONFIG" } }
// { dg-final { scan-assembler-not "SFPLOADMACRO" } }
// { dg-final { scan-assembler-times "SFPSETCC" 1 } }
// { dg-final { scan-assembler-times "SFPSTORE" 1 } }
// { dg-final { scan-assembler-times "TTINCRWC" 8 } }

#define SELECT_ADDR_MODE 3
#include "macro-planner-select-body.h"

__attribute__((noinline)) void select_rows ()
{
  SELECT_ROWS_8 ();
}
