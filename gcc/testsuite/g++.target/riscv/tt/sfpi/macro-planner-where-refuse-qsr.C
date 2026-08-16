// WP8: the quarantined pass's predicated-select (TTNN Where) positive,
// converted to the generic macro planner's named refusal.  The
// predicate write inside the slice would need a CC-manipulating
// instruction template; no proven CC-template program exists, so the
// region refuses cc-template-unsupported and every byte stays explicit.
// { dg-options "-mcpu=tt-qsr32-tensix -O3 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump "cc-template-unsupported" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-not "SFPLOADMACRO" } }
// { dg-final { scan-assembler-not "\\.ttinsn" } }
// { dg-final { scan-assembler-not "SFPCONFIG" } }
// { dg-final { scan-assembler-times "SFPSETCC" 1 } }
// { dg-final { scan-assembler-times "TTINCRWC" 1 } }
// { dg-final { scan-assembler-times "SFPSTORE" 1 } }

#include "macro-planner-where-body.inc"
