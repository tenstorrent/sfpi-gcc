// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2 -fno-unroll-loops -mtt-tensix-replay-shadow-discovery -fdump-rtl-rvtt_replay-details" }
// FABLE_GOES_BURR.md item #9 STAGE A -- the suffix automaton runs as a
// SHADOW beside the legacy grow-by-one discovery and never decides.
//
// FIRE + BOUNDARY NEAR-MISS IN ONE ROW.  Three copies of one six-word
// SFPU row, the first separated from the other two by an opaque asm.
// The automaton splices a UNIQUE separator symbol wherever the legacy
// extension's must_end check refuses to walk, so the string is reported
// as 2 segments: a unique symbol cannot occur twice, therefore no
// candidate can span the barrier BY CONSTRUCTION -- admission semantics
// cannot move.  Inside the segments all three copies are still one
// candidate, and the legacy picker (untouched) forms it.
// { dg-final { scan-rtl-dump "replay-maximal-repeats: bb \[0-9\]+: 19 symbols in 6 classes, 2 segments, 20 automaton states vs 28 grown sequences; maximal repeats 3 \\(left-maximal 3\\); legacy candidates 6, automaton candidates 6, automaton-only 0, superset OK \\(0 violations\\)" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "superset VIOLATED" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "replay-maximal-repeats-missed" "rvtt_replay" } }
// The picker still decides, and its pick is the legacy one: the 6-word
// row, three instances (a barrier-crossing 12-word candidate is not
// enumerable and is therefore never formed).
// { dg-final { scan-rtl-dump "Capturing and executing sequence \\\[0,6\\\) 3 instances to \\\[0,\\+6\\\) saving=9" "rvtt_replay" } }
// { dg-final { scan-assembler-times "TTREPLAY\t0, 6, 1, 1" 1 } }
#include "shadow-discovery-body.h"
void barrier_twin (unsigned n)
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
