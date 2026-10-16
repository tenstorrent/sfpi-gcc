// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fdump-rtl-rvtt_replay-details" }
// Check that SFPTRANSP alone does not block capture of the plain-SFPSWAP group.
// { dg-final { scan-rtl-dump "Capturing and executing sequence" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "Refusing capture: multiresult-companion-split" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "Refusing capture: shadow-state-unproved" "rvtt_replay" } }
// { dg-final { scan-assembler "SFPTRANSP" } }
// { dg-final { scan-assembler "TTREPLAY" } }

#include "multiresult-companion-split-body.h"

void transp_companion_split ()
{
  {
    auto v0 = __builtin_rvtt_sfpreadlreg (0);
    auto v1 = __builtin_rvtt_sfpreadlreg (1);
    auto v2 = __builtin_rvtt_sfpreadlreg (2);
    auto v3 = __builtin_rvtt_sfpreadlreg (3);
    auto c0 = __builtin_rvtt_sfpreadlreg (4);
    auto c1 = __builtin_rvtt_sfpreadlreg (5);
    auto c2 = __builtin_rvtt_sfpreadlreg (6);
    auto c3 = __builtin_rvtt_sfpreadlreg (7);
    auto r = __builtin_rvtt_sfptransp8 (v0, v1, v2, v3, c0, c1, c2, c3);
    __builtin_rvtt_sfpwritelreg (__builtin_rvtt_sfpselect4 (r, 0), 0);
    __builtin_rvtt_sfpwritelreg (__builtin_rvtt_sfpselect4 (r, 1), 1);
    __builtin_rvtt_sfpwritelreg (__builtin_rvtt_sfpselect4 (r, 2), 2);
    __builtin_rvtt_sfpwritelreg (__builtin_rvtt_sfpselect4 (r, 3), 3);
  }

  repeated_plain_swap_groups ();
}
