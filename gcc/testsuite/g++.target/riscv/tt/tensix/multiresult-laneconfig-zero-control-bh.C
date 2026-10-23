// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fdump-rtl-rvtt_replay-details" }
// An exact zero LaneConfig write is not a possible index-tracking enable.
// { dg-final { scan-rtl-dump "Capturing and executing sequence" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "Refusing capture: multiresult-companion-split" "rvtt_replay" } }
// { dg-final { scan-assembler "TTREPLAY" } }

#include "multiresult-companion-split-body.h"

void zero_laneconfig_does_not_enable_shadow ()
{
  __builtin_rvtt_sfpencc_all_lanes ();
  __builtin_rvtt_sfpconfig_i (0, 15, 1);
  repeated_plain_swap_groups ();
}
