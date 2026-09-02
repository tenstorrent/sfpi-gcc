// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2 -fno-unroll-loops -fdump-rtl-rvtt_replay-details" }
// Suffix-automaton discovery, STAGE A, OFF IDENTITY.  The same row as
// replay-shadow-discovery-barrier-bh.C with the measurement knob absent:
// the shadow prints nothing at all, so the pass's dump stream (which the
// twin suite and the board's dump mining key on) is byte-identical to
// the pre-item stream, and the formation is unchanged.
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
