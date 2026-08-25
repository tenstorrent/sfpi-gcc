// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-dst-autoincr -fdump-rtl-rvtt_dst_autoincr-details" }
// Exact loop-preheader amortization boundary for the uniform full cost.  A
// typed face advance re-anchors the RWC state, isolating configuration price:
// eight one-row trips tie config cost 8 and refuse, while nine trips pay it
// and fire.  The boundary follows from dynamic_rows, not cgraph visibility or
// a special loop discount.
// { dg-final { scan-rtl-dump "Dst-autoincr refusal: unprofitable group .config 8 >= removed 8" "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump-times "Dst-autoincr group: bb \[0-9\]+ rows 1 stride 2 config 3 words .preheader." 1 "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-times "TTINCRWC\t0, 2, 0, 0" 1 } }
// { dg-final { scan-assembler-times "SFPSTORE\tL., 0, 0, 6" 1 } }
// { dg-final { scan-assembler-times "SFPSTORE\tL., 0, 0, 7" 1 } }
// { dg-final { scan-assembler-times "TTSETC16\t18, 0" 1 } }
// { dg-final { scan-assembler-times "TTSETC16\t34, 2" 1 } }
// { dg-final { scan-assembler-times "TTSETC16\t53, 0" 1 } }

using boundary_vector_t = __xtt_vector;

#define DEFINE_BOUNDARY(NAME, TRIPS)                                         \
  void NAME ()                                                              \
  {                                                                         \
    for (unsigned ix = 0; ix != TRIPS; ++ix)                                \
      {                                                                     \
        boundary_vector_t x                                                 \
          = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);                \
        boundary_vector_t y = __builtin_rvtt_sfpmul (x, x, 0);              \
        __builtin_rvtt_sfpstore (nullptr, y, 0, 0, 0, 0, 7);                \
        __builtin_rvtt_ttincrwc (0, 2, 0, 0);                               \
        __builtin_rvtt_ttdstface ();                                        \
      }                                                                     \
  }

DEFINE_BOUNDARY (eight_trip_tie_refuses, 8)
DEFINE_BOUNDARY (nine_trip_profit_fires, 9)
