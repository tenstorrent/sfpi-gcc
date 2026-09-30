// Genericity twin (charter rule 3): renamed functions, varied Dst
// addresses (a different derived descriptor) -- the hoist derives from
// dataflow, the closure, and the descriptor's own content, never names
// or magic constants, so the identical stage-2 proof fires.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -mtt-tensix-optimize-init-hoist -fdump-rtl-rvtt_macro_planner" }
// { dg-final { scan-rtl-dump "Macro-planner init-hoist: stage=2" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "init-hoist=full" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-times "SFPENCC\\t3, 10" 1 } }

#define RESULT_INDEX 1
#define INIT_CALLER_NAME renamed_tile_driver
#define INIT_CALLER_PRELUDE()                                                 \
  do                                                                          \
    {                                                                         \
      asm volatile (".ttinsn %0" :: "n" (0xb2120000u));                       \
      asm volatile (".ttinsn %0" :: "n" (0xb2220002u));                       \
      asm volatile (".ttinsn %0" :: "n" (0xb2350000u));                       \
    }                                                                         \
  while (0)
#include "init-hoist-caller-body.h"
