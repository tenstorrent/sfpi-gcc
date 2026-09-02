// The ENABLE_DEST_INDEX write-restriction window model: a value-form LaneConfig write
// leaves the window state UNKNOWN; a companion-bank write under an
// unproven state is a dump NOTE, never an error (ROW_MASK traffic must
// not be punished).  The name had no testsuite coverage (FH-17).
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-crosslane -fdump-rtl-rvtt_crosslane_window" }

void unknown_state_write ()
{
  auto v = __builtin_rvtt_sfpreadlreg (0);
  __builtin_rvtt_sfpwriteconfig_v (v, 15);   // state -> UNKNOWN
  auto y = __builtin_rvtt_sfpand (v, v);
  __builtin_rvtt_sfpwritelreg (y, 5);        // L5 write, unproven state
}

// { dg-final { scan-rtl-dump "crosslane-window-state-unproven" "rvtt_crosslane_window" } }
