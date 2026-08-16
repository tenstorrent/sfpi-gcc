// QSR has no proven SFPLOADMACRO capability table: the loop-body region
// refuses target-macro-encoding-unproven and the explicit row stays.
// { dg-options "-mcpu=tt-qsr32-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump "target-macro-encoding-unproven" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-not "SFPLOADMACRO" } }
// { dg-final { scan-assembler-not "SFPCONFIG" } }
// { dg-final { scan-assembler-times "SFPSHFT" 1 } }
// { dg-final { scan-assembler-times "SFPCAST" 1 } }
// { dg-final { scan-assembler-times "TTINCRWC" 1 } }

#include "macro-planner-staged-loop-body.h"
