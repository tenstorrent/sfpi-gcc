// Stage 2: with every SETC16-class delivery to an owned row proven
// value-equal to the contract's encoded word and one such write
// dominating the loop, the FULL prefix -- enable, owned SETC16
// program, descriptor words -- programs once in the caller's loop
// preheader; the callee becomes pure payload (no enable, no SETC16,
// no SFPCONFIG).
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -mtt-tensix-optimize-init-hoist -fdump-rtl-rvtt_macro_planner" }
// { dg-final { scan-rtl-dump "Macro-planner init-hoist: stage=2 init contract hoisted to caller loop preheader \\(5 descriptor words, 3 setc16, enable hoisted\\)" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner formed: rows=32 runs=4 init-hoist=full" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-times "SFPENCC\\t3, 10" 1 } }
// { dg-final { scan-assembler-times "SFPCONFIG" 5 } }
// The hoisted owned SETC16 program spells as the typed TTSETC16 in the
// caller's preheader; the callee carries none (its only setc16-class
// words would be the retained per-call program, absent at stage 2 --
// the prelude's three seed words are the only raw ones).
// { dg-final { scan-assembler-times "TTSETC16\\t18, 0" 1 } }
// { dg-final { scan-assembler-times "TTSETC16\\t34, 2" 1 } }
// { dg-final { scan-assembler-times "TTSETC16\\t53, 0" 1 } }
// { dg-final { scan-assembler-times "\\.ttinsn.-1307443200" 1 } }

#define INIT_CALLER_PRELUDE()                                                 \
  do                                                                          \
    {                                                                         \
      asm volatile (".ttinsn %0" :: "n" (0xb2120000u));                       \
      asm volatile (".ttinsn %0" :: "n" (0xb2220002u));                       \
      asm volatile (".ttinsn %0" :: "n" (0xb2350000u));                       \
    }                                                                         \
  while (0)
#include "init-hoist-caller-body.h"
