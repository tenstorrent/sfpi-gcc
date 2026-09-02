// An ICE reproducer at source level (xielu fresh body with
// alpha_p/alpha_n held as LOOP-INVARIANT vFloats): runtime scalars
// materialize through two-issue SFPLOADI synthesis chains, and holding
// them across the loop overfills the LREG file -- the reference
// compiler crashed here ("cannot store sfpu register (register
// spill)", sfpi_funcs.h).  Under const-remat the scalar chains are
// re-issued inside the predicated arms that consume them (the GPR
// halves stay available), and the body compiles.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-const-remat -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump "const-remat: pressure \\d+ exceeds the 8-LREG file" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump "const-remat: rematerialized " "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump "const-remat: pressure resolved: \\d+ -> \\d+" "rvtt_prgm_const" } }

namespace ckernel { unsigned *instrn_buffer; }
#include <sfpi.h>

namespace {
sfpi_inline sfpi::vFloat round_bias_nearest (const sfpi::vFloat z,
					     sfpi::vInt &k_int)
{
  constexpr float ROUNDING_BIAS = 12582912.0f; // 1.5 * 2^23
  const sfpi::vFloat t = z + ROUNDING_BIAS;
  k_int = sfpi::as<sfpi::vInt> (t)
    - sfpi::as<sfpi::vInt> (sfpi::vFloat (ROUNDING_BIAS));
  return t - ROUNDING_BIAS;
}
union conv { unsigned u; float f; };
}

template <int ITERATIONS>
void calculate_xielu_loopheld (const unsigned param0, const unsigned param1)
{
  constexpr float EPS        = -1e-6f;
  constexpr float EXPM1_EPS  = -0.0000009999995427f;
  constexpr float ONE_LN2    = 1.4426950408889634f;
  constexpr float LN2_HI_NEG = -0.6931152343750000f;
  constexpr float LN2_LO_NEG = -3.19461832987e-05f;
  constexpr float T2 = 0.500000059604644775390625f;
  constexpr float T3 = 0.16666667163372039794921875f;
  constexpr float T4 = 4.16650883853435516357421875e-2f;
  constexpr float T5 = 8.333188481628894805908203125e-3f;
  constexpr float T6 = 1.400390756316483020782470703125e-3f;
  constexpr float T7 = 1.99588379473425447940826416015625e-4f;

  conv c0{param0}, c1{param1};
  const sfpi::vFloat valpha_p = c0.f;
  const sfpi::vFloat valpha_n = c1.f;
  for (int d = 0; d < ITERATIONS; ++d)
    {
      const sfpi::vFloat x          = sfpi::dst_reg[0];
      const sfpi::vFloat beta_mul_x = 0.5f * x;
      sfpi::vFloat result = (valpha_p * x) * x + beta_mul_x;
      v_if (x <= 0.0f && x >= EPS)
	{
	  result = valpha_n * (EXPM1_EPS - x) + beta_mul_x;
	}
      v_elseif (x < 0.0f && x > -0.5f)
	{
	  sfpi::vFloat p = T7;
	  p              = p * x + T6;
	  p              = p * x + T5;
	  p              = p * x + T4;
	  p              = p * x + T3;
	  p              = p * x + T2;
	  result         = valpha_n * (x * x * p) + beta_mul_x;
	}
      v_elseif (x <= -0.5f)
	{
	  sfpi::vFloat z = x * ONE_LN2;
	  z              = sfpi::max (z, -126.5f);
	  sfpi::vInt k_int;
	  const sfpi::vFloat k = round_bias_nearest (z, k_int);
	  const sfpi::vFloat r = k * LN2_LO_NEG + (k * LN2_HI_NEG + x);

	  sfpi::vFloat p = 1.0f / 5040.0f;
	  p              = p * r + 1.0f / 720.0f;
	  p              = p * r + 1.0f / 120.0f;
	  p              = p * r + 1.0f / 24.0f;
	  p              = p * r + 1.0f / 6.0f;
	  p              = p * r + 0.5f;
	  p              = p * r + 1.0f;
	  p              = p * r + 1.0f;

	  const sfpi::vFloat expx
	    = sfpi::setexp (p, sfpi::exexp (p, sfpi::ExponentMode::Biased)
			       + k_int);
	  result = valpha_n * (expx - 1.0f - x) + beta_mul_x;
	}
      v_endif;
      sfpi::dst_reg[0]
	= sfpi::convert<sfpi::vFloat16b> (result, sfpi::RoundMode::Nearest);
      sfpi::dst_reg++;
    }
}

template void calculate_xielu_loopheld<8> (unsigned, unsigned);
