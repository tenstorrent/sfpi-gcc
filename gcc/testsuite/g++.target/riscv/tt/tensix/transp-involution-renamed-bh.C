// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-transp-involution" }
// Renamed-equivalent twin of transp-involution-bh.C: every identifier
// differs; the formation must key on dataflow shape only.
// { dg-final { scan-assembler-times "SFPENCC" 1 } }
// { dg-final { scan-assembler-times "SFPTRANSP" 6 } }
// { dg-final { scan-assembler-times "SFPSTORE" 2 } }
// { dg-final { scan-assembler-times "SFPLOAD\t" 12 } }
using wide_lane_value = decltype (__builtin_rvtt_sfpreadlreg (9));

static inline void gathered_quad (wide_lane_value &alpha, wide_lane_value &beta,
				  int base_row)
{
  __builtin_rvtt_sfpstore (nullptr, alpha, 320, 0, 0, 3, 7);
  __builtin_rvtt_sfpstore (nullptr, beta, 328, 0, 0, 3, 7);
  auto r0 = __builtin_rvtt_sfpload (nullptr, 0 + 0, 0, 0, 0, 7);
  auto r1 = __builtin_rvtt_sfpload (nullptr, 0 + 2, 0, 0, 0, 7);
  auto r2 = __builtin_rvtt_sfpload (nullptr, 0 + 16, 0, 0, 0, 7);
  auto r3 = __builtin_rvtt_sfpload (nullptr, 0 + 18, 0, 0, 0, 7);
  (void) base_row;
  auto quad = __builtin_rvtt_sfptransp (r0, r1, r2, r3);
  r0 = __builtin_rvtt_sfpselect4 (quad, 0);
  r1 = __builtin_rvtt_sfpselect4 (quad, 1);
  r2 = __builtin_rvtt_sfpselect4 (quad, 2);
  r3 = __builtin_rvtt_sfpselect4 (quad, 3);
  alpha = __builtin_rvtt_sfpload (nullptr, 320, 0, 0, 3, 7);
  beta = __builtin_rvtt_sfpload (nullptr, 328, 0, 0, 3, 7);
  auto gap = __builtin_rvtt_sfpadd (r0, alpha, 0);
  alpha = __builtin_rvtt_sfpmad (gap, r1, alpha, 0);
  auto gap2 = __builtin_rvtt_sfpadd (r2, alpha, 0);
  beta = __builtin_rvtt_sfpmad (gap, gap2, beta, 0);
  (void) r3;
}

void reduce_over_lanes ()
{
  auto alpha = __builtin_rvtt_sfpreadlreg (9);
  auto beta = __builtin_rvtt_sfpreadlreg (9);
  gathered_quad (alpha, beta, 0);
  gathered_quad (alpha, beta, 1);
  gathered_quad (alpha, beta, 2);
  __builtin_rvtt_sfpstore (nullptr, alpha, 320, 0, 0, 3, 7);
  __builtin_rvtt_sfpstore (nullptr, beta, 328, 0, 0, 3, 7);
}
