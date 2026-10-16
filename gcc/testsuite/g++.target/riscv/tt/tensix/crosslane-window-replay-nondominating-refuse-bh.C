// Check that a launch in an open window whose record does not dominate it refuses by name.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-crosslane" }

using vec_t = __xtt_vector;
volatile unsigned pick;

void nondominating_record (int sel)
{
  if (sel)
    {
      __builtin_rvtt_ttreplay (nullptr, 4, 0, 0, 0, 1, 1);  // record on this arm only
      vec_t x = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      vec_t y = __builtin_rvtt_sfpand (x, x);
      __builtin_rvtt_sfpwritelreg (y, 5);
      vec_t z = __builtin_rvtt_sfpmul (y, y, 0);
      __builtin_rvtt_sfpstore (nullptr, z, 0, 0, 0, 0, 7);
    }
  else
    {
      __builtin_rvtt_sfpconfig_i (0x4, 15, 1);
      __builtin_rvtt_ttreplay (nullptr, 4, 0, 0, 0, 0, 0);  // record does not dominate
      __builtin_rvtt_sfpconfig_i (0x0, 15, 1);
    }
  pick = 1;
}

// { dg-error "crosslane-window-replay-unproven" "" { target *-*-* } 0 }
