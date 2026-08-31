// George-test near-miss (FABLE_GOES_BURR.md item #6): the copy's dest
// is precolored but dies BEFORE the anchors are read, while the source
// half lives across them -- the uncolored half has significant
// (precolored) neighbors that do NOT interfere with the precolored
// half, exactly what George's test exists to refuse (merging would
// glue the anchors' constraints onto a web they never interfered
// with).  Refuses coalesce-george-interference; spills unchanged.
// TODAY: refusal fires, 1 spill (same as flag-off).  FUTURE-VERDICT:
// append-only refusal name; the anatomy stays a refusal.
// { dg-options "-mcpu=tt-bh-tensix -O2 -mtt-tensix-optimize-lreg-alloc -mtt-tensix-optimize-lreg-coalesce -mtt-tensix-dst-layout-32b -fdump-rtl-rvtt_lp_alloc-details -fdump-rtl-rvtt_dst_ownership" }
// { dg-final { scan-rtl-dump "1 reload.s. folded" "rvtt_dst_ownership" } }
// { dg-final { scan-rtl-dump "coalesce-refusal: coalesce-george-interference" "rvtt_lp_alloc" } }
// { dg-final { scan-rtl-dump-not "lreg-alloc coalesce: merged web" "rvtt_lp_alloc" } }
// { dg-final { scan-rtl-dump-times "spilling web" 1 "rvtt_lp_alloc" } }
// { dg-final { scan-rtl-dump "colorability=proven" "rvtt_lp_alloc" } }

#define WL(x, n) __builtin_rvtt_sfpwritelreg ((x), (n))
#define ST(x, r) __builtin_rvtt_sfpstore (nullptr, (x), (r), 0, 0, 4, 7)

void coalesce_george_nearmiss (void)
{
  auto v = __builtin_rvtt_sfpload (nullptr, 320, 0, 0, 4, 7);
  auto w = __builtin_rvtt_sfpload (nullptr, 320, 0, 0, 4, 7); /* -> w = v */
  WL (w, 6);			/* w's only use: precolor L6, dead here */
  auto h = __builtin_rvtt_sfpxor (v, v);
  auto s2 = __builtin_rvtt_sfpxor (v, v);

  auto p0 = __builtin_rvtt_sfpreadlreg (0);
  auto p1 = __builtin_rvtt_sfpreadlreg (1);
  auto p2 = __builtin_rvtt_sfpreadlreg (2);
  auto p3 = __builtin_rvtt_sfpreadlreg (3);
  auto p4 = __builtin_rvtt_sfpreadlreg (4);
  auto p5 = __builtin_rvtt_sfpreadlreg (5);
  WL (p0, 0); WL (p0, 0); WL (p0, 0); WL (p0, 0); WL (p0, 0);
  WL (p1, 1); WL (p1, 1); WL (p1, 1); WL (p1, 1); WL (p1, 1);
  WL (p2, 2); WL (p2, 2); WL (p2, 2); WL (p2, 2); WL (p2, 2);
  WL (p3, 3); WL (p3, 3); WL (p3, 3); WL (p3, 3); WL (p3, 3);
  WL (p4, 4); WL (p4, 4); WL (p4, 4); WL (p4, 4); WL (p4, 4);
  WL (p5, 5); WL (p5, 5); WL (p5, 5); WL (p5, 5); WL (p5, 5);

  ST (h, 336); ST (h, 344); ST (h, 352); ST (h, 360);
  ST (h, 368); ST (h, 376); ST (h, 384); ST (h, 392);
  ST (s2, 408);
  ST (v, 400);			/* v live across the anchor defs */
}
