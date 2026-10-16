// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fdump-rtl-rvtt_replay-details" }
// Check that a plain-SFPSWAP capture refuses by name when an indexed SFPSWAP shows index tracking may be enabled.
// { dg-final { scan-rtl-dump "Refusing capture: multiresult-companion-split" "rvtt_replay" } }
// { dg-final { scan-assembler-not "TTREPLAY" } }

#include "multiresult-companion-split-body.h"

void nearmiss_companion_split ()
{
  // Typed multi-result operation: proves index tracking may be enabled.
  {
    auto v0 = __builtin_rvtt_sfpreadlreg (0);
    auto v2 = __builtin_rvtt_sfpreadlreg (2);
    auto i0 = __builtin_rvtt_sfpreadlreg (4);
    auto i2 = __builtin_rvtt_sfpreadlreg (6);
    auto r = __builtin_rvtt_sfpswap_indexed (v0, v2, i0, i2, 8);
    __builtin_rvtt_sfpwritelreg (__builtin_rvtt_sfpselect4 (r, 0), 0);
    __builtin_rvtt_sfpwritelreg (__builtin_rvtt_sfpselect4 (r, 1), 2);
    __builtin_rvtt_sfpwritelreg (__builtin_rvtt_sfpselect4 (r, 2), 4);
    __builtin_rvtt_sfpwritelreg (__builtin_rvtt_sfpselect4 (r, 3), 6);
  }

  repeated_plain_swap_groups ();
}
