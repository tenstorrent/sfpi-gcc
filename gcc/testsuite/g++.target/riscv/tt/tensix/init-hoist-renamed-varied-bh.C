#include "rtl-lane-reset.h"

// Check the stage-2 init hoist with renamed functions and different Dst addresses.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -mtt-tensix-optimize-init-hoist -fdump-rtl-rvtt_macro_planner" }
// { dg-final { scan-rtl-dump "Macro-planner init-hoist: stage=2" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "init-hoist=full" "rvtt_macro_planner" } }
// Two physical lane resets plus the hoisted planner-owned enable.
// { dg-final { scan-assembler-times "SFPENCC\\t3, 10" 3 } }

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
