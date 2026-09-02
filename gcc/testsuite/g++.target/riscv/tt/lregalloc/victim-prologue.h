/* Shared prologue for the real-kernel LREG-pressure victims (the
   allocator acceptance arsenal).  Provides the instruction-buffer stub the sfpi
   macros reference, plus the self-contained numeric helpers the victim
   bodies inline (restated from the tt-metal storm-contract fresh_cpp
   helpers; published identities, no LLK dependencies).  */

namespace ckernel {
unsigned *instrn_buffer;
}

#include <sfpi.h>
#include <limits>
#include <cstdint>

namespace victim {

/* Reciprocal of a strictly positive finite fp32 vector: Blinn bit-seed
   plus three Newton steps.  */
sfpi_inline sfpi::vFloat recip_positive (const sfpi::vFloat x)
{
  constexpr int RECIP_SEED_MAGIC = 0x7EF127EA;
  sfpi::vFloat r = sfpi::as<sfpi::vFloat> (sfpi::vInt (RECIP_SEED_MAGIC)
					   - sfpi::as<sfpi::vInt> (x));
  for (int step = 0; step < 3; ++step)
    r = r * (2.0f - x * r);
  return r;
}

/* sqrt for known-nonnegative input: reciprocal-sqrt bit seed, three
   Newton steps (fp32 dest), one Heron step on the product.  */
sfpi_inline sfpi::vFloat sqrt_ge0 (const sfpi::vFloat x)
{
  constexpr int SEED = 0x5f1110a0;
  const sfpi::vFloat half_x = 0.5f * x;
  sfpi::vFloat y = sfpi::as<sfpi::vFloat> (
      sfpi::vInt (SEED)
      - sfpi::as<sfpi::vInt> (sfpi::as<sfpi::vUInt> (x) >> 1));
  y = y * (1.5f - half_x * y * y);
  y = y * (1.5f - half_x * y * y);
  y = y * (1.5f - half_x * y * y);
  sfpi::vFloat a = x * y;
  a = a + 0.5f * (x - a * a) * y;
  v_if (x == 0.0f) { a = 0.0f; }
  v_endif;
  return a;
}

/* log1p by the Juffa reduction, deg-9 fp32-dest polynomial.  */
sfpi_inline sfpi::vFloat log1p_fp32 (const sfpi::vFloat a)
{
  constexpr float LOG_TWO_2M23 = 0.693147182f * 1.19209290e-7f;
  const sfpi::vFloat u = a + 1.0f;
  sfpi::vFloat r = std::numeric_limits<float>::quiet_NaN ();
  v_if (u >= 0.0f)
    {
      const sfpi::vFloat three_quarters = 0.75f;
      sfpi::vInt e = sfpi::as<sfpi::vInt> (u)
		     - sfpi::as<sfpi::vInt> (three_quarters);
      e = sfpi::as<sfpi::vInt> (sfpi::setman (sfpi::as<sfpi::vFloat> (e), 0));
      sfpi::vFloat m = sfpi::as<sfpi::vFloat> (sfpi::as<sfpi::vInt> (a) - e);
      const sfpi::vFloat neg_four = -4.0f;
      const sfpi::vFloat s
	  = sfpi::as<sfpi::vFloat> (sfpi::as<sfpi::vInt> (neg_four) - e);
      const sfpi::vFloat t = -0.25f * s - 1.0f;
      m = m + t;
      const auto abs_e = sfpi::abs (e);
      sfpi::vFloat e_float
	  = sfpi::convert<sfpi::vFloat> (abs_e, sfpi::RoundMode::Nearest);
      e_float = sfpi::copysgn (e_float, sfpi::as<sfpi::vFloat> (e));
      const sfpi::vFloat m_sq = m * m;
      sfpi::vFloat p;
      p = -0x1.92cp-5f;
      p = p * m + 0x1.b84p-4f;
      p = p * m + -0x1.0c4p-3f;
      p = p * m + 0x1.274p-3f;
      p = p * m + -0x1.55p-3f;
      p = p * m + 0x1.998p-3f;
      p = p * m + -0x1.00001ap-2f;
      p = p * m + 0x1.555572p-2f;
      p = p * m + -0.5f;
      r = p * m_sq + m;
      r = e_float * LOG_TWO_2M23 + r;
      const sfpi::vFloat infinity = std::numeric_limits<float>::infinity ();
      v_if (sfpi::as<sfpi::vInt> (u) >= sfpi::as<sfpi::vInt> (infinity))
	{
	  r = u;
	}
      v_endif;
    }
  v_endif;
  return r;
}

} // namespace victim
