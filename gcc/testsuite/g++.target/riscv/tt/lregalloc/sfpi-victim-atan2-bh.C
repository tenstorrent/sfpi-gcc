// REAL ICE VICTIM: atan2, the naive pre-restructure form (never
// committed -- reconstructed from the recorded disposition: quadrant folds
// applied AFTER the degree-9 polynomial, so ay/ax/x/y all stay live
// through t/t2/p plus the reciprocal's temporaries.  The shipped body
// pre-composes the folds into one affine map (B + F*p) BEFORE the
// polynomial so ay/ax die early -- restructure, not flags, was the fix.
// TODAY: refuses lreg-pressure-exceeded (the acosh-class refusal).
// FUTURE-VERDICT (LREG allocator): COMPILE via exact fp32 Dst-row
// spill; bit-exact vs the same-DAG hand-spilled twin (ARSENAL.md).
// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }
// { dg-error "lreg-pressure-exceeded" "" { target *-*-* } 0 }

#include "victim-prologue.h"

void victim_atan2_naive ()
{
  constexpr float PI = 3.14159265358979323846f;
  constexpr float PI_2 = 1.57079632679489661923f;
  constexpr float A1 = 0.99997726f;
  constexpr float A3 = -0.33262347f;
  constexpr float A5 = 0.19354346f;
  constexpr float A7 = -0.11643287f;
  constexpr float A9 = 0.05265332f;
  for (int d = 0; d < 8; ++d)
    {
      const sfpi::vFloat y = sfpi::dst_reg[0];
      const sfpi::vFloat x = sfpi::dst_reg[32];
      const sfpi::vFloat ay = sfpi::abs (y);
      const sfpi::vFloat ax = sfpi::abs (x);
      const sfpi::vFloat hi = sfpi::max (ay, ax);
      const sfpi::vFloat lo = sfpi::min (ay, ax);
      sfpi::vFloat t = lo * victim::recip_positive (hi);
      v_if (hi == 0.0f) { t = 0.0f; }
      v_endif;
      const sfpi::vFloat t2 = t * t;
      sfpi::vFloat p = ((((A9 * t2 + A7) * t2 + A5) * t2 + A3) * t2 + A1) * t;
      /* NAIVE: folds and special-case fixups AFTER the polynomial ->
	 ay/ax/x/y and hi/lo must stay live through t, t2, p and the
	 reciprocal's temporaries.  */
      v_if (ay > ax) { p = PI_2 - p; }
      v_endif;
      v_if (x < 0.0f) { p = PI - p; }
      v_endif;
      /* atan2 special cases, handled naively at the end: both zero ->
	 0; |x| infinite with |y| finite -> exact 0 or pi (the reduced
	 argument lo/hi would be NaN from inf/inf, so the fixups reread
	 ay, ax, hi and lo).  */
      v_if (hi == 0.0f) { p = 0.0f; }
      v_endif;
      constexpr float INF = std::numeric_limits<float>::infinity ();
      v_if (ax == INF && ay < INF)
	{
	  p = 0.0f;
	  v_if (x < 0.0f) { p = PI; }
	  v_endif;
	}
      v_endif;
      v_if (ay == INF && ax < INF) { p = PI_2; }
      v_endif;
      v_if (lo == hi && hi > 0.0f && ax < INF)
	{
	  /* Exact diagonal: pi/4 modulo the earlier folds.  */
	  p = 0.25f * PI;
	  v_if (ay > ax) { p = PI_2 - p; }
	  v_endif;
	  v_if (x < 0.0f) { p = PI - p; }
	  v_endif;
	}
      v_endif;
      sfpi::dst_reg[0] = sfpi::copysgn (p, y);
      sfpi::dst_reg++;
    }
}
