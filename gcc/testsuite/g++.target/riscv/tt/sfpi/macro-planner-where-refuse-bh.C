// WP9: the predicated-select (TTNN Where) single-row shape.  Before
// the 2026-08-17 Where silicon adjudication the 4-slot descriptor
// proved and Layer-6 profitability refused; the separator-kept
// schedule now refuses FIRST (evidence root
// ~/sfpi-uplift/where-adjudication-20260817): the mixed-mode compact
// candidate refuses its descriptor by name and the established
// calendar keeps the typed separator, refusing
// cc-separator-kept-silicon-unproven before any descriptor or
// profitability derivation.  Every byte stays explicit, as before.
// { dg-options "-mcpu=tt-bh-tensix -O3 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump "Macro-planner descriptor-refusal: cc-template-unproved" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner schedule-refusal: cc-separator-kept-silicon-unproven" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "Macro-planner descriptor-cc:" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "Macro-planner formed" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-not "SFPLOADMACRO" } }
// { dg-final { scan-assembler-not "\\.ttinsn" } }
// { dg-final { scan-assembler-not "SFPCONFIG" } }
// { dg-final { scan-assembler-times "SFPSETCC" 1 } }
// { dg-final { scan-assembler-times "TTINCRWC" 1 } }
// { dg-final { scan-assembler-times "SFPSTORE" 1 } }

#include "macro-planner-where-body.inc"
