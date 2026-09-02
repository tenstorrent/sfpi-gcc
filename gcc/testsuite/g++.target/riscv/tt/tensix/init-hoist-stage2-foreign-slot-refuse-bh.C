// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -mtt-tensix-optimize-init-hoist -fdump-rtl-rvtt_macro_planner" }
// Fail-closed twin (from an adversarial audit):
// the stage-2 owned-row value-equality proof read TU_FACTS.SLOT_WORDS
// without the SLOTS_UNPROVEN guard, and mop_init_ok_p (which checks)
// runs only when a MOP word is delivered inside the scanned epoch --
// while template words execute at their MOP sites before and between
// calls.  Here a FOREIGN rooted function programs a template slot with
// a word the census cannot resolve (slots UNPROVEN, no MOP in the
// caller epoch): the earlier binary still hoisted stage 2 over the
// vacuous equality (verified on the installed binary); the fixed
// compiler demotes to stage 1 by name.  Body-unavailability of the
// contract subject ITSELF stays excused (init-hoist-stage2-bh.C keeps
// firing) -- everything else fails closed.
// { dg-final { scan-rtl-dump "init-hoist: value-equality: mop slot census unproven .mop-template-slot-word-unresolved." "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "init-hoist: stage-2 demoted .value-equality-unproven." "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner init-hoist: stage=1 init contract hoisted to caller loop preheader \\(5 descriptor words, 3 setc16, enable retained\\)" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner formed: rows=32 runs=4 init-hoist=descriptor" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "stage=2 init contract hoisted" "rvtt_macro_planner" } }

#define INIT_CALLER_PRELUDE()                                                 \
  do                                                                          \
    {                                                                         \
      asm volatile (".ttinsn %0" :: "n" (0xb2120000u));                       \
      asm volatile (".ttinsn %0" :: "n" (0xb2220002u));                       \
      asm volatile (".ttinsn %0" :: "n" (0xb2350000u));                       \
    }                                                                         \
  while (0)
#define INIT_CALLER_NAME ihfs_caller_tiles
#include "init-hoist-caller-body.h"

// The foreign slot programmer: rooted (externally visible), walked by
// the census, storing a value the census cannot resolve to a constant.
typedef volatile unsigned int vu32;
void
ihfs_program (unsigned w)
{
  ((vu32 *) 0xFFB80000)[5] = w;
}
