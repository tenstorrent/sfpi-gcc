// Check that a launch with no accounting record inside an open window is a named error.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-crosslane" }

void unresolved_launch_in_window ()
{
  __builtin_rvtt_sfpconfig_i (0x4, 15, 1);
  __builtin_rvtt_ttreplay (nullptr, 4, 0, 0, 0, 0, 0);  // no record anywhere
  __builtin_rvtt_sfpconfig_i (0x0, 15, 1);
}

// { dg-error "crosslane-window-replay-unproven" "" { target *-*-* } 0 }
