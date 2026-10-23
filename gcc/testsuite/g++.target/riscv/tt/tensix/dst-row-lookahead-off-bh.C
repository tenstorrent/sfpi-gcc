// Check that without -mtt-tensix-optimize-dst-row-lookahead the row loop keeps its shape.
// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops" }
// { dg-final { scan-assembler-times "SFPLOAD\tL\[0-7\], 0, 0, 7" 1 } }
// { dg-final { scan-assembler-not "SFPLOAD\tL\[0-7\], 2, 0, 7" } }
// { dg-final { scan-assembler-times "SFPSTORE" 1 } }

using vec_t = __xtt_vector;

void
row_loop ()
{
  // Lane flags and LaneConfig ROW_MASK are independent hardware state.
  __builtin_rvtt_sfpencc_all_lanes ();
  __builtin_rvtt_sfpconfig_i (0, 15, 1);
  vec_t c0 = __builtin_rvtt_sfpreadlreg (12);
  vec_t c1 = __builtin_rvtt_sfpreadlreg (13);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      vec_t x = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      vec_t r = __builtin_rvtt_sfpmad (x, c0, c1, 0);
      __builtin_rvtt_sfpstore (nullptr, r, 0, 0, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
