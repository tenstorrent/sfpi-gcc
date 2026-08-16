// P0/D1: a lanes-off SFPENCC ahead of a formable eight-row Min/Max
// region is NOT an ambient all-lanes enable.  The written lane state is
// outside the CRAQ-proven store/misc envelope (the deleted quarantined
// pass required exactly the all-lanes operands and refused this case),
// so discovery refuses by name, the region keeps no enable, formation
// refuses the missing proof, and the bytes stay identical to flags-off
// (.s identity in oracles/cc-enable-refusal-manifest.txt).
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump-times "Macro-planner refusal: cc-enable-unproved" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner formation-refusal: all-lanes-proof-missing" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "Macro-planner formed" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-not "SFPLOADMACRO" } }
// { dg-final { scan-assembler-not "SFPCONFIG" } }
// { dg-final { scan-assembler-not "\\.ttinsn" } }
// The unproved enable itself stays in place, untouched.
// { dg-final { scan-assembler-times "SFPENCC" 1 } }
// (Replay formation may compress the identical explicit rows.)
// { dg-final { scan-assembler "SFPSWAP" } }
// { dg-final { scan-assembler-times "TTINCRWC" 8 } }

#define CC_ENABLE_STMT __builtin_rvtt_sfpencc (0, 10)
#include "macro-planner-cc-enable-body.h"
