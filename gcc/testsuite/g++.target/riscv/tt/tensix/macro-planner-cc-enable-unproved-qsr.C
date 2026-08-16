// QSR twin of the P0/D1 lanes-off refusal: the ambient-enable proof
// refuses by the same name before QSR's missing capability table
// refuses the schedule -- behavior consistent across CPUs, bytes
// flags-off everywhere.
// { dg-options "-mcpu=tt-qsr32-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump-times "Macro-planner refusal: cc-enable-unproved" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "Macro-planner formed" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-not "SFPLOADMACRO" } }
// { dg-final { scan-assembler-not "SFPCONFIG" } }
// { dg-final { scan-assembler-times "SFPENCC" 1 } }
// { dg-final { scan-assembler "SFPSWAP" } }
// { dg-final { scan-assembler-times "TTINCRWC" 8 } }

#define CC_ENABLE_STMT __builtin_rvtt_sfpencc (0, 10)
#include "macro-planner-cc-enable-body.h"
