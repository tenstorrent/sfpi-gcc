// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fdump-rtl-rvtt_replay-details" }
// Nonzero, runtime and vector writes remain possible enables.  A later zero
// reset must not erase the function-sticky possibility of an earlier enable.
// { dg-final { scan-rtl-dump-times "Refusing capture: multiresult-companion-split" 4 "rvtt_replay" } }
// { dg-final { scan-assembler-not "TTREPLAY" } }

#include "multiresult-companion-split-body.h"

void nonzero_laneconfig ()
{
  __builtin_rvtt_sfpconfig_i (1, 15, 1);
  repeated_plain_swap_groups ();
}

void earlier_possible_enable_then_zero ()
{
  __builtin_rvtt_sfpconfig_i (4, 15, 1);
  __builtin_rvtt_sfpencc_all_lanes ();
  __builtin_rvtt_sfpconfig_i (0, 15, 1);
  repeated_plain_swap_groups ();
}

void runtime_laneconfig (volatile unsigned long *buffer, unsigned value)
{
  auto config = __builtin_rvtt_sfpxloadi (buffer, value, 0, 0, -32);
  __builtin_rvtt_sfpwriteconfig_v (config, 0, 15);
  repeated_plain_swap_groups ();
}

void vector_laneconfig ()
{
  auto value = __builtin_rvtt_sfpreadlreg (3);
  __builtin_rvtt_sfpwriteconfig_v (value, 0, 15);
  repeated_plain_swap_groups ();
}
