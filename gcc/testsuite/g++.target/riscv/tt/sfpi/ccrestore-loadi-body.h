/* Counted Dst loop with a v_if region and loop-invariant immediates inside
   and outside it; CCR_* macros vary names and values.  */

extern volatile unsigned long __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned long (&instrn_buffer)[] = ::__instrn_buffer;
}

#include <sfpi.h>

#ifndef CCR_TRIPS
#define CCR_TRIPS 32
#endif

__attribute__((noinline)) void
CCR_FN ()
{
  for (int ix = 0; ix < CCR_TRIPS; ++ix)
    {
      sfpi::vFloat v = sfpi::dst_reg[0];
      sfpi::vFloat r = v * CCR_COEFF;
      v_if (CCR_COND (v))
	{
	  r = sfpi::vFloat (CCR_SPECIAL) * v;
	}
      v_endif;
      sfpi::dst_reg[0] = r;
      sfpi::dst_reg++;
    }
}
