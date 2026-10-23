#include "../tensix/rtl-lane-reset.h"

// Check that on Quasar the predicated-select row refuses macro scheduling (target-macro-encoding-unproven).
// { dg-options "-mcpu=tt-qsr32-tensix -O3 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump "target-macro-encoding-unproven" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "Macro-planner formed" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-not "SFPLOADMACRO" } }
// { dg-final { scan-assembler-not "\\.ttinsn" } }
// { dg-final { scan-assembler-not "SFPCONFIG" } }
// { dg-final { scan-assembler-times "SFPSETCC" 1 } }
// { dg-final { scan-assembler-times "TTINCRWC" 1 } }
// { dg-final { scan-assembler-times "SFPSTORE" 1 } }

#include "macro-planner-where-body.inc"
