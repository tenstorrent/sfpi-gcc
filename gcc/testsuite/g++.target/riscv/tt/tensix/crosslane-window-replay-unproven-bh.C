// Fail-closed launch resolution: a playback launch whose slots
// no record in the function accounts for delivers device-persistent
// content this compilation cannot audit -- inside a proven-OPEN window
// that is a named error, never a silent acceptance.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-crosslane" }

void unresolved_launch_in_window ()
{
  __builtin_rvtt_sfpconfig_i (0x4, 15, 1);
  __builtin_rvtt_ttreplay (nullptr, 4, 0, 0, 0, 0, 0);  // no record anywhere
  __builtin_rvtt_sfpconfig_i (0x0, 15, 1);
}

// { dg-error "crosslane-window-replay-unproven" "" { target *-*-* } 0 }
