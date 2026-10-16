/* A noinline macro-formed minmax callee called from a counted caller loop;
   INIT_CALLER_PRELUDE() and INIT_LOOP_TAIL() let variants add code.  */

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
