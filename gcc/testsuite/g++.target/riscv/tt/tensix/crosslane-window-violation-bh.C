// the ENABLE_DEST_INDEX write-restriction erratum negative twin (X4): a non-exempt instruction
// computing a value INTO the companion bank inside an open
// ENABLE_DEST_INDEX window -- the class caught in the wild as
// an allocator-inserted `SFPMOV L5, L4` -- is a named compile error on
// the final stream.  The write is forced by pinning the in-window
// arithmetic result to LReg5.
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
