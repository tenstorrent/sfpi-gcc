#ifndef RVTT_TEST_LANE_RESET
#define RVTT_TEST_LANE_RESET() ((void) 0)
#endif

/* A noinline macro-formed minmax callee called from a counted caller loop;
   INIT_CALLER_PRELUDE() and INIT_LOOP_TAIL() let variants add code.  */

#define MINMAX_FOUR_FACE_RUNS 1
#define MINMAX_VISIBILITY static
#include "loadmacro-periodic-minmax-inplace-body.h"

#ifndef INIT_CALLER_PRELUDE
#define INIT_CALLER_PRELUDE()
#endif
#ifndef INIT_LOOP_TAIL
#define INIT_LOOP_TAIL()
#endif
#ifndef INIT_CALLER_NAME
#define INIT_CALLER_NAME caller_tiles
#endif

__attribute__((noinline)) void INIT_CALLER_NAME (unsigned tiles)
{
  RVTT_TEST_LANE_RESET ();
  INIT_CALLER_PRELUDE ();
  unsigned t = 0;
  do
    {
      periodic_minmax_inplace ();
      __builtin_rvtt_ttdstface ();
      INIT_LOOP_TAIL ();
    }
  while (++t < tiles);
}
