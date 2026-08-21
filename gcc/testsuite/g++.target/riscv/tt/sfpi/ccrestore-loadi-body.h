/* Parameterized kernel-shaped predicated row loop: a counted Dst loop
   whose body carries a structured v_if CC region (a special-case
   rewrite of the row value) and loop-invariant immediate
   materializations both outside and inside the region.  The CCR_*
   macros are free names and values: the structured-CC-restore proof
   keys only on the balanced plain-PUSHC/plain-POPC structure and the
   audited narrowing modifier class -- never on operation names,
   constants, or shapes -- so the decisions below must be identical
   under any renaming and any varied constants.  */

extern volatile unsigned __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned (&instrn_buffer)[] = ::__instrn_buffer;
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
