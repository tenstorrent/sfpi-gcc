// Cross-call invariant-init hoist (D2), stage 1: the callee's
// derived descriptor program (five SFPCONFIG words and their staged
// loads) is call-invariant descriptor data; the caller's loop epoch
// proves LoadMacroConfig-clean, so the words program once in the loop
// preheader (with their own canonical all-lanes enable for the
// lane-predicated staging loads) instead of once per call.  With no
// dominating value-equal reaching configuration for the owned SETC16
// rows, stage 2 demotes: the callee retains its per-call enable and
// owned SETC16 program byte-identically.
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
