// Check that a lanes-off SFPENCC refuses the ambient-enable proof on QSR.
// { dg-options "-mcpu=tt-qsr32-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump-times "Macro-planner refusal: cc-enable-unproved" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "Macro-planner formed" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-not "SFPLOADMACRO" } }
// { dg-final { scan-assembler-not "SFPCONFIG" } }
// { dg-final { scan-assembler-times "SFPENCC" 1 } }
// { dg-final { scan-assembler "SFPSWAP" } }
// { dg-final { scan-assembler-times "TTINCRWC" 8 } }

#define CC_ENABLE_STMT __builtin_rvtt_sfpencc (10, 0)
#include "macro-planner-cc-enable-body.h"
