// MOP-delivery refusal (the mop-form composition row):
// the MOP expander re-delivers replay playback words from a template
// the window checker cannot audit positionally -- a TTMOP inside a
// proven-OPEN window errors by name (the counted playback loop re-rolls
// into one TTMOP at the default threshold, mop-form-counted shape).
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -fno-exceptions -fno-rtti -mtt-tensix-optimize-crosslane -mtt-tensix-optimize-mop-form" }

void mop_in_window ()
{
  __builtin_rvtt_ttreplay (nullptr, 3, 0, 0, 0, 1, 1);
  auto x = __builtin_rvtt_sfpreadlreg (0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  __builtin_rvtt_sfpwritelreg (x, 0);
  __builtin_rvtt_sfpconfig_i (0x4, 15, 1);
  for (unsigned i = 0; i != 20; ++i)
    __builtin_rvtt_ttreplay (nullptr, 3, 0, 0, 0, 0, 0);
  __builtin_rvtt_sfpconfig_i (0x0, 15, 1);
}

// { dg-error "crosslane-window-mop-unproven" "" { target *-*-* } 0 }
