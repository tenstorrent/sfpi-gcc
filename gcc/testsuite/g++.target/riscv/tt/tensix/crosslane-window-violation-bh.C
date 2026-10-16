// Check that a non-exempt write into LReg5 inside an open ENABLE_DEST_INDEX window is a
// named error.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-crosslane" }

void in_window_write ()
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  __builtin_rvtt_sfpconfig_i (0x4, 15, 1);
  auto y = __builtin_rvtt_sfpand (x, x);
  __builtin_rvtt_sfpwritelreg (y, 5);
  __builtin_rvtt_sfpconfig_i (0x0, 15, 1);
}

// { dg-error "dest-index-window-violation" "" { target *-*-* } 0 }
