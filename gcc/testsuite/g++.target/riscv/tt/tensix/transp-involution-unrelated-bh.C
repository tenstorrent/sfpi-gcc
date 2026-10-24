// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-transp-involution" }
// Check that transpose-involution bundles form in a transposed-gather max scan without parks.
// { dg-final { scan-assembler-times "SFPENCC" 1 } }
// { dg-final { scan-assembler-times "SFPTRANSP" 4 } }
using sfpu_t = decltype (__builtin_rvtt_sfpreadlreg (9));

static inline sfpu_t transposed_gather_sum (int, sfpu_t seed)
{
  auto a = __builtin_rvtt_sfpload (nullptr, 64, 0, 0, 0, 7);
  auto b = __builtin_rvtt_sfpload (nullptr, 66, 0, 0, 0, 7);
  auto c = __builtin_rvtt_sfpload (nullptr, 80, 0, 0, 0, 7);
  auto d = __builtin_rvtt_sfpload (nullptr, 82, 0, 0, 0, 7);
  auto r = __builtin_rvtt_sfptransp (a, b, c, d,
                                     __builtin_rvtt_sfpreadlreg (9), __builtin_rvtt_sfpreadlreg (9), __builtin_rvtt_sfpreadlreg (9), __builtin_rvtt_sfpreadlreg (9));
  a = __builtin_rvtt_sfpselect8 (r, 0);
  b = __builtin_rvtt_sfpselect8 (r, 1);
  c = __builtin_rvtt_sfpselect8 (r, 2);
  d = __builtin_rvtt_sfpselect8 (r, 3);
  seed = __builtin_rvtt_sfpmad (a, b, seed, 0);
  return __builtin_rvtt_sfpmad (c, d, seed, 0);
}

void unrelated_transpose_scan ()
{
  __builtin_rvtt_sfpencc_all_lanes ();
  __builtin_rvtt_sfpconfig_i (0, 15, 1);
  auto seed = __builtin_rvtt_sfpreadlreg (9);
  seed = transposed_gather_sum (0, seed);
  seed = transposed_gather_sum (1, seed);
  __builtin_rvtt_sfpstore (nullptr, seed, 640, 0, 0, 3, 7);
}
