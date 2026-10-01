// Near miss (ownership): a delivered SFPCONFIG-class word inside the
// caller's loop could rewrite LoadMacroConfig between calls -- the
// hoist refuses by the mission-named ownership refusal and the callee
// keeps its whole per-call prefix byte-identically.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -mtt-tensix-optimize-init-hoist -fdump-rtl-rvtt_macro_planner" }
// { dg-final { scan-rtl-dump "Macro-planner init-hoist-refusal: drain-init-ownership-unproven" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "init contract hoisted" "rvtt_macro_planner" } }
// The interposed raw SFPCONFIG-class word stays a raw word; the
// callee keeps its five typed SFPCONFIG writes per call.
// { dg-final { scan-assembler-times "SFPCONFIG" 5 } }
// { dg-final { scan-assembler-times "\\.ttinsn.-1862270911" 1 } }
// { dg-final { scan-assembler-times "SFPENCC\\t3, 10" 1 } }

#define INIT_LOOP_TAIL()                                                      \
  asm volatile (".ttinsn %0" :: "n" (0x91000041u))
#include "init-hoist-caller-body.h"
