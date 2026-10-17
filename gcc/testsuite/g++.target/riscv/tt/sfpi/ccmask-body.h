/* Counted Dst loop that zeroes a dependent result on the non-positive side
   of a float compare against +0.0; CCMASK_* macros vary names and values.  */

extern volatile unsigned long __instrn_buffer[];

namespace ckernel {
constexpr inline volatile unsigned long (&instrn_buffer)[] = ::__instrn_buffer;
}

#include <sfpi.h>

#ifndef CCMASK_COND
#define CCMASK_COND(x) ((x) <= 0.0f)
#endif

#ifndef CCMASK_ZERO
#define CCMASK_ZERO 0.0f
#endif

__attribute__((noinline)) void
CCMASK_FN ()
{
  for (int ix = 0; ix < 8; ++ix)
    {
      sfpi::vFloat CCMASK_X = sfpi::dst_reg[0];
      sfpi::vFloat CCMASK_Y = CCMASK_X * CCMASK_A + CCMASK_B;
      sfpi::vFloat CCMASK_W = CCMASK_Y;
      v_if (CCMASK_COND (CCMASK_X)) { CCMASK_W = CCMASK_ZERO; }
      v_endif;
      sfpi::dst_reg[0] = CCMASK_W + CCMASK_Y;
      sfpi::dst_reg++;
    }
}
