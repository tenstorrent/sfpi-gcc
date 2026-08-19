// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -mtt-tensix-optimize-init-hoist -fdump-rtl-rvtt_macro_planner" }
// ICE regression (wave-9 finding 13b): the SAME MOP-in-loop init-hoist
// shape under an in-TU `_start' root.  On the wave-9 base compiler the
// rooted census walked already-expanded closure bodies as gimple at
// planner time and SEGFAULTED; it must instead fail closed by name and
// keep forming.  (Compiling at all is the load-bearing assertion.)
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
