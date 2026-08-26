/* Parameterized kernel-shaped predicated row for the native-compare
   twins: a counted Dst loop whose body predicates an update on a float
   order test.  The NC_* macros are free names and values: the lowering
   decision must be identical under any renaming and any surrounding
   arithmetic (the GT/LE arms key only on the compare direction reaching
   rvtt_emit_sfpxfcmps/v).  */

extern volatile unsigned __instrn_buffer[];

namespace ckernel {
constexpr inline volatile unsigned (&instrn_buffer)[] = ::__instrn_buffer;
}

#include <sfpi.h>

#ifndef NC_COND
#define NC_COND(x) ((x) > 0.0f)
#endif

__attribute__((noinline)) void
NC_FN ()
{
  for (int ix = 0; ix < 8; ++ix)
    {
      sfpi::vFloat NC_X = sfpi::dst_reg[0];
      sfpi::vFloat NC_Y = NC_X * NC_A + NC_B;
      v_if (NC_COND (NC_X)) { NC_Y = NC_Y + NC_A; }
      v_endif;
      sfpi::dst_reg[0] = NC_Y;
      sfpi::dst_reg++;
    }
}
