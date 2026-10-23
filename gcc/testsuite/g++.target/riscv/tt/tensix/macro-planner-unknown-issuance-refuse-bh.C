// Unknown delivery/recording is not a region-local configuration contract.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump-not {Macro-planner formed:} "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times {Macro-planner formation-refusal:} 2 "rvtt_macro_planner" } }
// { dg-final { scan-assembler-not {SFPLOADMACRO} } }

#include "rtl-lane-reset.h"
#include "macro-planner-typecast-faces-body.h"
extern void unknown_config_owner ();

void unknown_raw_issue (unsigned faces)
{
  RVTT_TEST_LANE_RESET ();
  asm volatile (".ttinsn %0" :: "n" (0xff000004));
  for (unsigned face = 0; face < faces; ++face)
    { __builtin_rvtt_sfppushc (0); __builtin_rvtt_sfppopc (0); FACE (); }
}

void unknown_callee_issue (unsigned faces)
{
  unknown_config_owner ();
  RVTT_TEST_LANE_RESET ();
  for (unsigned face = 0; face < faces; ++face)
    { __builtin_rvtt_sfppushc (0); __builtin_rvtt_sfppopc (0); FACE (); }
}
