#include "rtl-lane-reset.h"

// Check that the signbit loop forms identical words under different names.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner" }
// { dg-final { scan-assembler-times "SFPENCC" 2 } }
// { dg-final { scan-assembler-times "SFPCONFIG" 5 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2467348480" 1 } }
// { dg-final { scan-assembler-times "SFPNOP" 3 } }
// { dg-final { scan-assembler-not "SFPSHFT" } }
// { dg-final { scan-assembler-not "TTINCRWC" } }

#if __riscv_xtttensixwh
constexpr unsigned keep = 3;
#else
constexpr unsigned keep = 7;
#endif

__attribute__((noinline)) void completely_unrelated_name (unsigned twilight)
{
  RVTT_TEST_LANE_RESET ();
  for (unsigned zebra = 0; zebra < twilight; ++zebra)
    {
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfppopc (0);
      auto aardvark = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, keep);
      auto banana = __builtin_rvtt_sfpshft_i (nullptr, aardvark, -31, 0, 0, 0);
      auto cactus = __builtin_rvtt_sfpcast (banana, 0);
      __builtin_rvtt_sfpstore (nullptr, cactus, 0, 0, 0, 0, keep);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
