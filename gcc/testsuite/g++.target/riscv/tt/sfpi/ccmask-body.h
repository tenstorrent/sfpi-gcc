/* Parameterized kernel-shaped predicated-zeroing row: a counted Dst
   loop whose body computes an arbitrary derived value and zeroes a
   dependent result on the non-positive side of a float order test
   against +0.0.  The CCMASK_* macros are free names and values: the
   fold's decision must be identical under any renaming and any
   surrounding arithmetic (the pass keys only on the structured CC
   dataflow shape, the +0.0 boundary, and the architectural zero being
   assigned).  */

extern volatile unsigned __instrn_buffer[];

namespace ckernel {
constexpr inline volatile unsigned (&instrn_buffer)[] = ::__instrn_buffer;
}

#include <sfpi.h>

#ifndef CCMASK_COND
#define CCMASK_COND(x) ((x) <= 0.0f)
#endif

__attribute__((noinline)) void
CCMASK_FN ()
{
  for (int ix = 0; ix < 8; ++ix)
    {
      sfpi::vFloat CCMASK_X = sfpi::dst_reg[0];
      sfpi::vFloat CCMASK_Y = CCMASK_X * CCMASK_A + CCMASK_B;
      sfpi::vFloat CCMASK_W = CCMASK_Y;
      v_if (CCMASK_COND (CCMASK_X)) { CCMASK_W = 0.0f; }
      v_endif;
      sfpi::dst_reg[0] = CCMASK_W + CCMASK_Y;
      sfpi::dst_reg++;
    }
}
