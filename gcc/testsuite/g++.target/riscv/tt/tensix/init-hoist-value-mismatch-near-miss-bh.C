// Near miss (stage-2 value equality): the caller's reaching
// configuration writes owned row 18 with a DIFFERENT value, so hoisting
// the owned SETC16 program would change what the first trip's readers
// observe -- stage 2 demotes and the callee retains its per-call
// enable + SETC16 program byte-identically (stage 1 still fires for
// the descriptor words, whose only readers are the callee's own
// launches).
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -mtt-tensix-optimize-init-hoist -fdump-rtl-rvtt_macro_planner" }
// { dg-final { scan-rtl-dump "init-hoist: value-equality: row 18 write unequal" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "init-hoist: stage-2 demoted \\(value-equality-unproven\\)" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner init-hoist: stage=1" "rvtt_macro_planner" } }
// The callee retains its per-call owned program (raw word form).
// { dg-final { scan-assembler-times "\\.ttinsn\\t2987524096" 1 } }
// { dg-final { scan-assembler-not "TTSETC16" } }

#define INIT_CALLER_PRELUDE()                                                 \
  asm volatile (".ttinsn %0" :: "n" (0xb2120004u))
#include "init-hoist-caller-body.h"
