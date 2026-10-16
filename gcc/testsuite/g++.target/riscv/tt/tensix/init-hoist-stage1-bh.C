// Check that the stage-1 init hoist moves the descriptor words to the caller's loop preheader.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -mtt-tensix-optimize-init-hoist -fdump-rtl-rvtt_macro_planner" }
// { dg-final { scan-rtl-dump "Macro-planner init-hoist: stage=1 init contract hoisted to caller loop preheader \\(5 descriptor words, 3 setc16, enable retained\\)" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner formed: rows=32 runs=4 init-hoist=descriptor" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-times "SFPENCC\\t3, 10" 2 } }
// { dg-final { scan-assembler-times "SFPCONFIG" 5 } }
// The retained per-call owned SETC16 program prints as the planner's
// raw words (0xb2120000 / 0xb2220002 / 0xb2350000); no caller-side
// typed TTSETC16 appears at stage 1.
// { dg-final { scan-assembler-times "\\.ttinsn\\t2987524096" 1 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2988572674" 1 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2989817856" 1 } }
// { dg-final { scan-assembler-not "TTSETC16" } }

#include "init-hoist-caller-body.h"
