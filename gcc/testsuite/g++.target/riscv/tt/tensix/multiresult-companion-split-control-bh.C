// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fdump-rtl-rvtt_replay-details" }
// Control for the companion-split near-miss: WITHOUT any typed evidence of
// index tracking in the function, the identical repeated plain-SFPSWAP
// group is an ordinary capture-and-execute formation.  This proves the
// near-miss's refusal is the discriminator, not an accident of the body.
//
// { dg-final { scan-rtl-dump "Capturing and executing sequence" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "Refusing capture: multiresult-companion-split" "rvtt_replay" } }
// { dg-final { scan-assembler "TTREPLAY" } }

#include "multiresult-companion-split-body.h"

void control_companion_split ()
{
  repeated_plain_swap_groups ();
}
