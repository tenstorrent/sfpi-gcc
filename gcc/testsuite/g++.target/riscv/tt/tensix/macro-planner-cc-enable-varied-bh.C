// Varied-constant twins of the P0/D1 refusal: every pure CC write whose
// encoded word is not exactly the architectural all-lanes enable
// refuses, whatever the constants -- partial imm12 selections and the
// swapped-operand-role spelling alike.  (The typed rvtt_sfpencc
// template prints "%1, %0" and the assembler reads "SFPENCC imm12,
// mod1", so operand 1 encodes imm12 and operand 0 mod1: the proven
// all-lanes instruction carries operands (10, 3), the same roles the
// deleted quarantined pass proved against.  A source-level
// __builtin_rvtt_sfpencc (3, 10) therefore EMITS imm12=10/mod1=3 --
// not the all-lanes instruction -- and must refuse.)
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
#define CC_ENABLE_STMT __builtin_rvtt_sfpencc (1, 10)
#include "macro-planner-cc-enable-body.h"

#define CC_ENABLE_FN partial_mask_result_bit
#define CC_ENABLE_STMT __builtin_rvtt_sfpencc (2, 10)
#define CC_ENABLE_LOAD1_ADDR 32
#define CC_ENABLE_STORE_ADDR 192
#include "macro-planner-cc-enable-body.h"

#define CC_ENABLE_FN swapped_operand_roles
#define CC_ENABLE_STMT __builtin_rvtt_sfpencc (3, 10)
#include "macro-planner-cc-enable-body.h"
