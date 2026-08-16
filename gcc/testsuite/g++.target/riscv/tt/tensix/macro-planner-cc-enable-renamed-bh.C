// Renamed-equivalent A/B on the ambient-enable proof: two functions
// with fresh names and varied addresses share the identical row
// structure; the ONLY difference is the enable ahead of the rows.  The
// proven all-lanes enable (pushc/popc-synthesized SFPENCC) forms; the
// lanes-off enable refuses by name with flags-off bytes.  Formation
// therefore keys on the proven lane state alone -- never on names,
// addresses, or shape identity.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump-times "Macro-planner refusal: cc-enable-unproved" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner formed: rows=8 runs=1" 1 "rvtt_macro_planner" } }
// The refused twin keeps its explicit shape; the formed twin's rows are
// replaced by the macro calendar.
// { dg-final { scan-assembler-times "SFPENCC" 2 } }
// { dg-final { scan-assembler-times "SFPCONFIG" 5 } }
// { dg-final { scan-assembler-times "\\.ttinsn" 19 } }
// { dg-final { scan-assembler-times "TTINCRWC" 8 } }
// { dg-final { scan-assembler "SFPSWAP" } }

#define CC_ENABLE_FN zephyr_gate_probe
#define CC_ENABLE_STMT __builtin_rvtt_sfpencc (0, 10)
#define CC_ENABLE_LOAD1_ADDR 96
#define CC_ENABLE_STORE_ADDR 160
#include "macro-planner-cc-enable-body.h"

#define CC_ENABLE_FN quill_formation_probe
#define CC_ENABLE_LOAD1_ADDR 96
#define CC_ENABLE_STORE_ADDR 160
#include "macro-planner-cc-enable-body.h"
