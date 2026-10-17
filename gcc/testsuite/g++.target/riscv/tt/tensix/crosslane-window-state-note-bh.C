// Check that a companion-bank write under an UNKNOWN window state is a dump note, not an error.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-crosslane -fdump-rtl-rvtt_crosslane_window" }

void unknown_state_write ()
{
  auto v = __builtin_rvtt_sfpreadlreg (0);
  __builtin_rvtt_sfpwriteconfig_v (v, 0, 15);   // state -> UNKNOWN
  auto y = __builtin_rvtt_sfpand (v, v);
  __builtin_rvtt_sfpwritelreg (y, 5);        // L5 write, unproven state
}

// { dg-final { scan-rtl-dump "crosslane-window-state-unproven" "rvtt_crosslane_window" } }
