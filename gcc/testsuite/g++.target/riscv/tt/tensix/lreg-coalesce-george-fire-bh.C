#include "rtl-lane-reset.h"

// Check that George's test merges a copy into a precolored destination with no spills.
// { dg-options "-mcpu=tt-bh-tensix -O2 -mtt-tensix-optimize-lreg-alloc -mtt-tensix-optimize-lreg-coalesce -mtt-tensix-dst-layout-32b -fdump-rtl-rvtt_lp_alloc-details -fdump-rtl-rvtt_dst_ownership" }
// { dg-final { scan-rtl-dump "1 reload.s. folded" "rvtt_dst_ownership" } }
// { dg-final { scan-rtl-dump "lreg-alloc coalesce: merged web r\\d+ into r\\d+ .george test" "rvtt_lp_alloc" } }
// { dg-final { scan-rtl-dump "after 0 spill.s., 0 round-trip" "rvtt_lp_alloc" } }
// { dg-final { scan-rtl-dump-not "spilling web" "rvtt_lp_alloc" } }
// { dg-final { scan-assembler-times {\mSFPLOAD\t} 1 } }
// { dg-final { scan-assembler-times {\mSFPSTORE\t} 2 } }

#define WL(x, n) __builtin_rvtt_sfpwritelreg ((x), (n))
#define ST(x, r) __builtin_rvtt_sfpstore (nullptr, (x), (r), 0, 0, 4, 7)

void coalesce_george_fire (void)
{
  RVTT_TEST_LANE_RESET ();
  auto v = __builtin_rvtt_sfpload (nullptr, 320, 0, 0, 4, 7);
  auto w = __builtin_rvtt_sfpload (nullptr, 320, 0, 0, 4, 7); /* -> w = v */
  auto h = __builtin_rvtt_sfpxor (v, v);

  auto p0 = __builtin_rvtt_sfpreadlreg (0);
  auto p1 = __builtin_rvtt_sfpreadlreg (1);
  auto p2 = __builtin_rvtt_sfpreadlreg (2);
  auto p3 = __builtin_rvtt_sfpreadlreg (3);
  auto p4 = __builtin_rvtt_sfpreadlreg (4);
  auto p5 = __builtin_rvtt_sfpreadlreg (5);
  WL (p0, 0); WL (p1, 1); WL (p2, 2); WL (p3, 3); WL (p4, 4); WL (p5, 5);

  ST (h, 336);
  ST (v, 344);			/* v live across the anchor defs */
  WL (w, 6);			/* w's last use: precolors w to L6 */
}
