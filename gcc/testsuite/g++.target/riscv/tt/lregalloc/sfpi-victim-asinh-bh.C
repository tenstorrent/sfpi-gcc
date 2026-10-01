// REAL ICE VICTIM: asinh, the clean fused form (the '+' side of the
// recorded flagship acosh/asinh cleanup diff).
// a = |x| and x2 = x*x HELD in vFloats across the sqrt/reciprocal
// expression AND the log1p polynomial, no DST round-trip: the shipped
// production kernel recomputes them inline and round-trips through DST
// purely to duck the 8-LREG wall (in-tree comment,
// ckernel_sfpu_trigonometry.h:1086-1089).
// TODAY: refuses lreg-pressure-exceeded.
// FUTURE-VERDICT (LREG allocator): COMPILE via exact fp32 Dst-row spill;
// numeric contract = the op's existing golden (the spill round-trip is
// exact, the DAG is the shipped math).
// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }
// { dg-error "lreg-pressure-exceeded" "" { target *-*-* } 0 }

#include "victim-prologue.h"

void victim_asinh_clean ()
{
  constexpr float LOG1P_LARGE = 1.0e18f;
  constexpr float LN2 = 0.69314718056f;
  for (int d = 0; d < 8; ++d)
    {
      sfpi::vFloat inp = sfpi::dst_reg[0];
      const sfpi::vFloat a = sfpi::abs (inp);
      const sfpi::vFloat x2 = inp * inp;
      sfpi::vFloat arg = 0.0f;
      v_if (a >= LOG1P_LARGE) { arg = a - 1.0f; }
      v_elseif (a >= 0.75f)
	{
	  sfpi::vFloat root = victim::sqrt_ge0 (x2 + 1.0f);
	  arg = a + x2 * victim::recip_positive (1.0f + root);
	}
      v_endif;
      sfpi::vFloat res = victim::log1p_fp32 (arg);
      v_if (a >= LOG1P_LARGE) { res = res + LN2; }
      v_endif;
      v_if (a < 0.75f)
	{
	  sfpi::vFloat s = x2;
	  sfpi::vFloat p = 4.375355784e-03f;
	  p = p * s + -1.484858524e-02f;
	  p = p * s + 2.785361186e-02f;
	  p = p * s + -4.209749034e-02f;
	  p = p * s + 7.495806366e-02f;
	  p = p * s + -1.666652262e-01f;
	  p = p * s + 1.000000000e+00f;
	  res = a * p;
	}
      v_endif;
      sfpi::dst_reg[0] = sfpi::copysgn (res, inp);
      sfpi::dst_reg++;
    }
}
