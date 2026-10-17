// Check that of two renamed functions only the one with a proven all-lanes enable forms.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump-times "Macro-planner refusal: cc-enable-unproved" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner formed: rows=8 runs=1" 1 "rvtt_macro_planner" } }
// The refused function keeps its explicit rows; the formed one's rows are
// replaced by the macro calendar.
// { dg-final { scan-assembler-times "SFPENCC" 2 } }
// { dg-final { scan-assembler-times "SFPCONFIG" 5 } }
// { dg-final { scan-assembler-times "\\.ttinsn" 19 } }
// { dg-final { scan-assembler-times "TTINCRWC" 8 } }
// { dg-final { scan-assembler "SFPSWAP" } }

#define CC_ENABLE_FN zephyr_gate_probe
#define CC_ENABLE_STMT __builtin_rvtt_sfpencc (10, 0)
#define CC_ENABLE_LOAD1_ADDR 96
#define CC_ENABLE_STORE_ADDR 160
#include "macro-planner-cc-enable-body.h"

#define CC_ENABLE_FN quill_formation_probe
#define CC_ENABLE_LOAD1_ADDR 96
#define CC_ENABLE_STORE_ADDR 160
#include "macro-planner-cc-enable-body.h"
