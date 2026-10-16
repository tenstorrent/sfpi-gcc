// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fdump-rtl-rvtt_replay-details" }
// Check that the plain-SFPSWAP group is captured when nothing shows index tracking enabled.
// { dg-final { scan-rtl-dump "Capturing and executing sequence" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "Refusing capture: multiresult-companion-split" "rvtt_replay" } }
// { dg-final { scan-assembler "TTREPLAY" } }

#include "multiresult-companion-split-body.h"

void control_companion_split ()
{
  repeated_plain_swap_groups ();
}
