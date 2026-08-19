// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -mtt-tensix-optimize-init-hoist -fdump-rtl-rvtt_macro_planner" }
// Production-shaped MOP coverage for the D2 init-hoist family (none
// existed: every committed test's caller loop was MOP-free, so
// mop_init_ok_p was never consulted).  A MOP launch inside the tile
// loop of an externally-entered `main' TU consults the TU template
// census at planner (RTL) time, when the contract subject's own body
// is already expanded: the census must fail closed BY NAME -- the
// wave-9 base compiler either fired vacuously (unrooted TU) or
// SEGFAULTED (rooted TU, gsi walk of an expanded body) on this shape.
// Formation itself must survive without the init hoist.
// { dg-final { scan-rtl-dump "Macro-planner init-hoist-refusal: mop-template-body-unavailable" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner formed: rows=32 runs=4" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "init contract hoisted" "rvtt_macro_planner" } }

#define INIT_CALLER_NAME ihme_caller_tiles
#define INIT_LOOP_TAIL()                                                      \
  do                                                                          \
    {                                                                         \
      asm volatile (".ttinsn %0" :: "n" (0x01800000u));	/* TTI_MOP */         \
      asm volatile (".ttinsn %0" :: "n" (0x37000104u));                       \
    }                                                                         \
  while (0)
#include "init-hoist-caller-body.h"

int
main ()
{
  ihme_caller_tiles (4);
  return 0;
}
