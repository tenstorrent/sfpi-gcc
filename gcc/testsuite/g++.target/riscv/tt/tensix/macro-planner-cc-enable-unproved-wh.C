#include "rtl-lane-reset.h"

// Check that a lanes-off SFPENCC refuses the ambient-enable proof on Wormhole.
// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump-times "Macro-planner refusal: cc-enable-unproved" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner formation-refusal: all-lanes-proof-missing" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "Macro-planner formed" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-not "SFPLOADMACRO" } }
// { dg-final { scan-assembler-not {SFPCONFIG[ \t]+([0-9]|1[0-4]),} } }
// { dg-final { scan-assembler-not "\\.ttinsn" } }
// { dg-final { scan-assembler-times "SFPENCC" 2 } }
// { dg-final { scan-assembler "SFPSWAP" } }
// { dg-final { scan-assembler-times "TTINCRWC" 8 } }

#define CC_ENABLE_STMT __builtin_rvtt_sfpencc (10, 0)
#include "macro-planner-cc-enable-body.h"
