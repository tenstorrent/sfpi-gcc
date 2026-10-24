// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-dst-autoincr -fdump-rtl-rvtt_dst_autoincr-details" }
// Check that an SFPSTOCHRND in each row does not block the preheader slot program.
// { dg-final { scan-rtl-dump-not "foreign effect" "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump-times "Dst-autoincr group: bb \[0-9\]+ rows 4 stride \[24\] config 3 words .preheader." 2 "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-not "TTINCRWC" } }
// { dg-final { scan-assembler-times "SFPSTOCHRND" 8 } }

using vec_t = __xtt_vector;

void
round_rows (unsigned n)
{
  for (unsigned ix = 0; ix != n; ++ix)
    {
      vec_t a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      vec_t p = __builtin_rvtt_sfpstochrnd (__builtin_rvtt_sfpmul (a, a, 0), 1, 0);
      __builtin_rvtt_sfpstore (nullptr, p, 0, 0, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
      vec_t b = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      vec_t q = __builtin_rvtt_sfpstochrnd (__builtin_rvtt_sfpmul (b, b, 0), 1, 0);
      __builtin_rvtt_sfpstore (nullptr, q, 0, 0, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
      vec_t c = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      vec_t r = __builtin_rvtt_sfpstochrnd (__builtin_rvtt_sfpmul (c, c, 0), 1, 0);
      __builtin_rvtt_sfpstore (nullptr, r, 0, 0, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
      vec_t d = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      vec_t s = __builtin_rvtt_sfpstochrnd (__builtin_rvtt_sfpmul (d, d, 0), 1, 0);
      __builtin_rvtt_sfpstore (nullptr, s, 0, 0, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}

void
renamed_varied_rounding (unsigned count)
{
  for (unsigned step = 0; step != count; ++step)
    {
      vec_t north = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      vec_t ne = __builtin_rvtt_sfpstochrnd (__builtin_rvtt_sfpadd (north, north, 0), 0, 0);
      __builtin_rvtt_sfpstore (nullptr, ne, 0, 0, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 4, 0, 0);
      vec_t east = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      vec_t ee = __builtin_rvtt_sfpstochrnd (__builtin_rvtt_sfpadd (east, east, 0), 0, 0);
      __builtin_rvtt_sfpstore (nullptr, ee, 0, 0, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 4, 0, 0);
      vec_t south = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      vec_t se = __builtin_rvtt_sfpstochrnd (__builtin_rvtt_sfpadd (south, south, 0), 0, 0);
      __builtin_rvtt_sfpstore (nullptr, se, 0, 0, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 4, 0, 0);
      vec_t west = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      vec_t we = __builtin_rvtt_sfpstochrnd (__builtin_rvtt_sfpadd (west, west, 0), 0, 0);
      __builtin_rvtt_sfpstore (nullptr, we, 0, 0, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 4, 0, 0);
    }
}
