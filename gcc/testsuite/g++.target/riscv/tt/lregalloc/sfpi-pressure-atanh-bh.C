// Check that atanh feeding the reciprocal into log1p and reusing |x| afterwards is refused for LREG pressure.
// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }
// { dg-error "lreg-pressure-exceeded" "" { target *-*-* } 0 }

#include "pressure-prologue.h"

void pressure_atanh_fused ()
{
  for (int d = 0; d < 8; ++d)
    {
      const sfpi::vFloat inp = sfpi::dst_reg[0];
      const sfpi::vFloat a = sfpi::abs (inp);
      /* atanh(|x|) = 0.5 * log1p(2|x| / (1 - |x|)), fused: the
	 reciprocal product feeds log1p directly.  */
      const sfpi::vFloat den = 1.0f - a;
      sfpi::vFloat arg = (a + a) * pressure::recip_positive (den);
      sfpi::vFloat res = 0.5f * pressure::log1p_fp32 (arg);
      /* Boundary fixups reuse the cached a after log1p.  */
      v_if (a >= 1.0f)
	{
	  res = std::numeric_limits<float>::infinity ();
	}
      v_endif;
      v_if (a > 1.0f)
	{
	  res = std::numeric_limits<float>::quiet_NaN ();
	}
      v_endif;
      sfpi::dst_reg[0] = sfpi::copysgn (res, inp);
      sfpi::dst_reg++;
    }
}
