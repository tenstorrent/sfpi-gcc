// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fdump-rtl-rvtt_replay-details" }
// Near-miss: an index-tracking-enabled operation whose companion the
// capture would split must refuse BY NAME.  The typed indexed SFPSWAP is
// architectural evidence that LaneConfig.ENABLE_DEST_INDEX may be enabled
// in this function (shadow coupling is function-sticky: there is no
// proven-off transition).  The repeated plain-SFPSWAP group below -- which
// the control test proves replay formation captures -- must therefore
// refuse: the plain swap moves the value bank while its companion
// movement is not typed in its pattern, so capturing it would split the
// coupled pair.  Refusal is byte-identical: no TTREPLAY may be formed.
//
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
