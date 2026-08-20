// REAL PRESSURE VICTIM (reconstruction): Welford prefix-mean/M2 in the
// pre-restructure shape -- the four gathered inputs x0..x3 held live
// through the WHOLE block together with the mean/m2 accumulators and
// the per-step reciprocal weights held as vFloats.  The shipped body
// (tt-metal sfpu_welford_prefix_snapshot.cpp, commit e51ccb2c6a) stores
// x0..x3 to their trace slots IMMEDIATELY after the gather, with the
// in-source reason: "they overflow the 8-lreg file together with the
// accumulators and the fold temporaries".  Here the trace capture sits
// AFTER the accumulation block (the naive dataflow order) and the
// weights are loop-held vFloats: the overflow shape.
// TODAY: refuses lreg-pressure-exceeded.
// FUTURE-VERDICT (LREG allocator): COMPILE via exact fp32 Dst-row
// spill; bit-exact vs the same-DAG hand-spilled twin (ARSENAL.md).
// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }
// { dg-error "lreg-pressure-exceeded" "" { target *-*-* } 0 }

#include "victim-prologue.h"

template <unsigned N>
sfpi_inline void welford_step (sfpi::vFloat x, sfpi::vFloat recip,
			       sfpi::vFloat &mean, sfpi::vFloat &m2)
{
  /* VFLOAT_DIRECT delta ordering (Impl == 2 in the shipped body).  */
  sfpi::vFloat delta = x - mean;
  sfpi::vFloat next_mean = mean + delta * recip;
  sfpi::vFloat next_m2 = m2 + delta * (x - next_mean);
  mean = next_mean;
  m2 = next_m2;
}

void victim_welford_predelta ()
{
  /* Per-step reciprocal weights held live as vFloats (non-special
     mantissas; 1/2 and 1/4 are NOT constant-creg foldable either --
     only 0, +-1 and 0.8373 fold).  */
  const sfpi::vFloat r1 = 1.0f / 3.0f;
  const sfpi::vFloat r2 = 1.0f / 5.0f;
  const sfpi::vFloat r3 = 1.0f / 7.0f;
  const sfpi::vFloat r4 = 1.0f / 9.0f;
  for (int blk = 0; blk < 4; ++blk)
    {
      /* Gather four inputs; naive order keeps them live to the end of
	 the block (trace capture BELOW the accumulation).  */
      const sfpi::vFloat x0 = sfpi::dst_reg[0];
      const sfpi::vFloat x1 = sfpi::dst_reg[8];
      const sfpi::vFloat x2 = sfpi::dst_reg[16];
      const sfpi::vFloat x3 = sfpi::dst_reg[24];
      sfpi::vFloat mean = sfpi::dst_reg[32];
      sfpi::vFloat m2 = sfpi::dst_reg[40];
      welford_step<1> (x0, r1, mean, m2);
      welford_step<2> (x1, r2, mean, m2);
      welford_step<3> (x2, r3, mean, m2);
      welford_step<4> (x3, r4, mean, m2);
      sfpi::dst_reg[32] = mean;
      sfpi::dst_reg[40] = m2;
      /* Trace capture at the END: the pre-restructure dataflow.  */
      sfpi::dst_reg[48] = x0;
      sfpi::dst_reg[49] = x1;
      sfpi::dst_reg[50] = x2;
      sfpi::dst_reg[51] = x3;
      sfpi::dst_reg++;
    }
}
