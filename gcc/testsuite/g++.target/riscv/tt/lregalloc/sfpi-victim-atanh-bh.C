// REAL ICE VICTIM (reconstruction): atanh, the fused form the in-tree
// comments document as overflowing the reload budget
// (ckernel_sfpu_trigonometry.h ~1163: "the fused form overflows it";
// ~1174: "a cached |x| - 1 variant pushed the allocator past the reload
// budget").  Reconstructed: the reciprocal->log1p expression is passed
// DIRECTLY (no DST round-trip) and a = |x| is CACHED and reused by the
// boundary fixups after log1p.
// TODAY: refuses lreg-pressure-exceeded.
// FUTURE-VERDICT (LREG allocator): COMPILE via exact fp32 Dst-row
// spill; numeric contract = the op's existing golden.
// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }
// { dg-error "lreg-pressure-exceeded" "" { target *-*-* } 0 }

#include "victim-prologue.h"

void victim_atanh_fused ()
{
  for (int d = 0; d < 8; ++d)
    {
      const sfpi::vFloat inp = sfpi::dst_reg[0];
      const sfpi::vFloat a = sfpi::abs (inp);
      /* atanh(|x|) = 0.5 * log1p(2|x| / (1 - |x|)), fused: the
	 reciprocal product feeds log1p directly.  */
      const sfpi::vFloat den = 1.0f - a;
      sfpi::vFloat arg = (a + a) * victim::recip_positive (den);
      sfpi::vFloat res = 0.5f * victim::log1p_fp32 (arg);
      /* Boundary fixups reuse the cached a AFTER log1p.  */
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
