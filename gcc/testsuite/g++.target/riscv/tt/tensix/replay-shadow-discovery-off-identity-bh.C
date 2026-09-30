// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2 -fno-unroll-loops -fdump-rtl-rvtt_replay-details" }
// Suffix-automaton discovery with the shadow measurement not requested:
// the shadow analysis prints nothing, the pass's dump is unaffected, and
// replay formation is unchanged.
// { dg-final { scan-rtl-dump-not "replay-maximal-repeats" "rvtt_replay" } }
// { dg-final { scan-rtl-dump "Capturing and executing sequence \\\[0,6\\\) 3 instances to \\\[0,\\+6\\\) saving=9" "rvtt_replay" } }
// { dg-final { scan-assembler-times "TTREPLAY\t0, 6, 1, 1" 1 } }
#include "shadow-discovery-body.h"
void barrier_twin_off (unsigned n)
{
  auto a = __builtin_rvtt_sfpreadlreg (0);
  auto b = __builtin_rvtt_sfpreadlreg (1);
  auto c = __builtin_rvtt_sfpreadlreg (2);
  for (unsigned ix = 0; ix != n; ++ix)
    {
      RUN (a, b, c)
      asm volatile ("" ::: "memory");
      RUN (a, b, c)
      RUN (a, b, c)
    }
  __builtin_rvtt_sfpwritelreg (a, 0);
  __builtin_rvtt_sfpwritelreg (b, 1);
  __builtin_rvtt_sfpwritelreg (c, 2);
}
