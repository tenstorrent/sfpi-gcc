// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -mtt-tensix-optimize-init-hoist -fdump-rtl-rvtt_macro_planner" }
// Renamed + varied twin of the entry-rooted init-hoist MOP refusal: a
// differently-named extern "C" entry (no `main'), a different caller
// name, and a runtime trip count.  The fail-closed verdict must key on
// the census facts, never on names.
// { dg-final { scan-rtl-dump "Macro-planner init-hoist-refusal: mop-template-body-unavailable" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner formed: rows=32 runs=4" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "init contract hoisted" "rvtt_macro_planner" } }

#define INIT_CALLER_NAME zv_tile_pump
#define INIT_LOOP_TAIL()                                                      \
  do                                                                          \
    {                                                                         \
      asm volatile (".ttinsn %0" :: "n" (0x01800000u));	/* TTI_MOP */         \
      asm volatile (".ttinsn %0" :: "n" (0x37000104u));                       \
    }                                                                         \
  while (0)
#include "init-hoist-caller-body.h"

extern "C" void
zv_kernel_entry (unsigned zn)
{
  zv_tile_pump (zn);
}
