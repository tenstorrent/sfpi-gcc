// WP9: the predicated-select (TTNN Where) shape is now a PROVEN
// CC-template program -- the descriptor derives and verifies the full
// CC model -- but a straight-line SINGLE row cannot amortize the
// configuration prefix, so the derived Layer-6 profitability refuses
// and every byte stays explicit.  (Before WP9 this refused
// cc-template-unsupported at discovery; the multi-row formation twins
// are macro-planner-select-form-* in ../tensix and
// macro-planner-where-form-bh.C here.)
// { dg-options "-mcpu=tt-bh-tensix -O3 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump "Macro-planner descriptor-cc: sense=complement" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "formation-refusal: unprofitable" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "Macro-planner formed" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-not "SFPLOADMACRO" } }
// { dg-final { scan-assembler-not "\\.ttinsn" } }
// { dg-final { scan-assembler-not "SFPCONFIG" } }
// { dg-final { scan-assembler-times "SFPSETCC" 1 } }
// { dg-final { scan-assembler-times "TTINCRWC" 1 } }
// { dg-final { scan-assembler-times "SFPSTORE" 1 } }

#include "macro-planner-where-body.inc"
