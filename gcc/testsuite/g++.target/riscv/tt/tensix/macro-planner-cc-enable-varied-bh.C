// Check that any CC write other than the exact all-lanes SFPENCC (imm12 10, mod1 3) refuses formation.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump-times "Macro-planner refusal: cc-enable-unproved" 3 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner formation-refusal: all-lanes-proof-missing" 3 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "Macro-planner formed" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-not "SFPLOADMACRO" } }
// { dg-final { scan-assembler-not "SFPCONFIG" } }
// { dg-final { scan-assembler-not "\\.ttinsn" } }
// { dg-final { scan-assembler-times "SFPENCC" 3 } }
// { dg-final { scan-assembler-times "TTINCRWC" 24 } }

#define CC_ENABLE_FN partial_mask_enable_bit
#define CC_ENABLE_STMT __builtin_rvtt_sfpencc (10, 1)
#include "macro-planner-cc-enable-body.h"

#define CC_ENABLE_FN partial_mask_result_bit
#define CC_ENABLE_STMT __builtin_rvtt_sfpencc (10, 2)
#define CC_ENABLE_LOAD1_ADDR 32
#define CC_ENABLE_STORE_ADDR 192
#include "macro-planner-cc-enable-body.h"

#define CC_ENABLE_FN varied_legal_operand_roles
#define CC_ENABLE_STMT __builtin_rvtt_sfpencc (0, 10)
#include "macro-planner-cc-enable-body.h"
