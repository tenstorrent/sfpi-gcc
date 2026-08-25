/* Cross-call invariant-init hoist bodies (lane CA, D2): the production
   minmax shape -- a noinline per-tile callee whose macro formation
   emits the idempotent init prefix, called from a counted caller
   loop.  The callee body is the established in-place periodic minmax
   (loadmacro-periodic-minmax-inplace-body.h, four face runs).
   INIT_CALLER_PRELUDE() lets variants seed the caller's reaching
   configuration (the stage-2 value-equality subject);
   INIT_LOOP_TAIL() lets near-misses interpose a loop follower.  */

#define MINMAX_FOUR_FACE_RUNS 1
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
