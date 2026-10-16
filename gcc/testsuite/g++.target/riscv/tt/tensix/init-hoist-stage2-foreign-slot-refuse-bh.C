// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -mtt-tensix-optimize-init-hoist -fdump-rtl-rvtt_macro_planner" }
// Check that an unresolvable template slot written by another rooted function demotes the init hoist to stage 1.
// { dg-final { scan-rtl-dump "init-hoist: value-equality: mop slot scan unproven .mop-template-slot-word-unresolved." "rvtt_macro_planner" } }
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
// the scan, storing a value the scan cannot resolve to a constant.
typedef volatile unsigned int vu32;
void
ihfs_program (unsigned w)
{
  ((vu32 *) 0xFFB80000)[5] = w;
}
