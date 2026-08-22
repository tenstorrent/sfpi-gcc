// TEN-2932 window model (lane FP audit): a call inside a proven-OPEN
// ENABLE_DEST_INDEX window cannot be audited -- named compile error
// (the name had no testsuite coverage; FH-17 discipline).
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-crosslane" }

extern void helper ();

void call_in_window ()
{
  __builtin_rvtt_sfpconfig_i (0x4, 15, 1);
  helper ();
  __builtin_rvtt_sfpconfig_i (0x0, 15, 1);
}

// { dg-error "crosslane-window-call-unproven" "" { target *-*-* } 0 }
