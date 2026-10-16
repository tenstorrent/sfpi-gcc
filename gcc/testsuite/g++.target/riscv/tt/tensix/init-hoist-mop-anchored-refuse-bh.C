// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -mtt-tensix-optimize-init-hoist -fdump-rtl-rvtt_macro_planner" }
// Check that a MOP in the caller loop under an in-TU `_start' root refuses the init hoist without an ICE.
// { dg-final { scan-rtl-dump "Macro-planner init-hoist-refusal: mop-template-body-unavailable" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner formed: rows=32 runs=4" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "init contract hoisted" "rvtt_macro_planner" } }

#define INIT_CALLER_NAME ihma_caller_tiles
#define INIT_LOOP_TAIL()                                                      \
  do                                                                          \
    {                                                                         \
      asm volatile (".ttinsn %0" :: "n" (0x01800000u));	/* TTI_MOP */         \
      asm volatile (".ttinsn %0" :: "n" (0x37000104u));                       \
    }                                                                         \
  while (0)
#include "init-hoist-caller-body.h"

extern "C" void
_start ()
{
  ihma_caller_tiles (4);
}
