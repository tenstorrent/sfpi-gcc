// Baseline half of the Briggs-coalescing fire twin (FABLE_GOES_BURR.md
// item #6).  A dst-ownership identity-reload fold mints the only
// non-interfering plain copy that reaches lp-alloc (out-of-SSA has
// already coalesced every tree-visible one).  Uncoalesced, the copy
// web is the cheapest member of the blocked neighborhood and is picked
// FIRST -- a wasted victim (the block comes from the six precolored
// anchors plus the heavy chain) -- so the allocator round-trips BOTH
// the copy web and the real victim: 2 spills, 10 round-trip words.
// The +coalesce twin (lreg-coalesce-fire-bh.C) merges the copy and
// spills only the real victim.  TODAY: compiles with 2 spills.
// FUTURE-VERDICT: if a later pin changes victim pricing, re-adjudicate
// against the twin pair -- the invariant is spills(base) >
// spills(coalesce) on this body, never the absolute counts.
// { dg-options "-mcpu=tt-bh-tensix -O2 -mtt-tensix-optimize-lreg-alloc -mtt-tensix-dst-layout-32b -fdump-rtl-rvtt_lp_alloc-details -fdump-rtl-rvtt_dst_ownership" }
// { dg-final { scan-rtl-dump "1 reload.s. folded" "rvtt_dst_ownership" } }
// { dg-final { scan-rtl-dump-times "spilling web" 2 "rvtt_lp_alloc" } }
// { dg-final { scan-rtl-dump-not "lreg-alloc coalesce:" "rvtt_lp_alloc" } }
// { dg-final { scan-rtl-dump "colorability=proven" "rvtt_lp_alloc" } }
// { dg-final { scan-assembler-times {\mSFPLOAD\t} 9 } }
// { dg-final { scan-assembler-times {\mSFPSTORE\t} 26 } }

#define WL(x, n) __builtin_rvtt_sfpwritelreg ((x), (n))
#define ST(x, r) __builtin_rvtt_sfpstore (nullptr, (x), (r), 0, 0, 4, 7)

void coalesce_fire_base (void)
{
  /* Fold source + identity reload: dst-ownership replaces the reload
     with the plain copy w = v.  */
  auto v  = __builtin_rvtt_sfpload (nullptr, 320, 0, 0, 4, 7);
  auto s1 = __builtin_rvtt_sfpxor (v, v);
  auto w  = __builtin_rvtt_sfpload (nullptr, 320, 0, 0, 4, 7);

  /* Heavy chain: h takes L6, h1 takes L7, h2 blocks (its palette is
     the six anchors plus h and h1).  */
  auto h  = __builtin_rvtt_sfpxor (s1, s1);
  auto h1 = __builtin_rvtt_sfpxor (h, h);
  auto h2 = __builtin_rvtt_sfpxor (h1, h1);
  ST (w, 328);			/* w's last use, after h2's def */

  auto p0 = __builtin_rvtt_sfpreadlreg (0);
  auto p1 = __builtin_rvtt_sfpreadlreg (1);
  auto p2 = __builtin_rvtt_sfpreadlreg (2);
  auto p3 = __builtin_rvtt_sfpreadlreg (3);
  auto p4 = __builtin_rvtt_sfpreadlreg (4);
  auto p5 = __builtin_rvtt_sfpreadlreg (5);

  /* Occurrence ballast: keeps every anchor more expensive than the
     heavies, and the heavies more expensive than the copy web.  */
  WL (p0, 0); WL (p0, 0); WL (p0, 0); WL (p0, 0); WL (p0, 0);
  WL (p0, 0); WL (p0, 0); WL (p0, 0); WL (p0, 0); WL (p0, 0);
  WL (p1, 1); WL (p1, 1); WL (p1, 1); WL (p1, 1); WL (p1, 1);
  WL (p1, 1); WL (p1, 1); WL (p1, 1); WL (p1, 1); WL (p1, 1);
  WL (p2, 2); WL (p2, 2); WL (p2, 2); WL (p2, 2); WL (p2, 2);
  WL (p2, 2); WL (p2, 2); WL (p2, 2); WL (p2, 2); WL (p2, 2);
  WL (p3, 3); WL (p3, 3); WL (p3, 3); WL (p3, 3); WL (p3, 3);
  WL (p3, 3); WL (p3, 3); WL (p3, 3); WL (p3, 3); WL (p3, 3);
  WL (p4, 4); WL (p4, 4); WL (p4, 4); WL (p4, 4); WL (p4, 4);
  WL (p4, 4); WL (p4, 4); WL (p4, 4); WL (p4, 4); WL (p4, 4);
  WL (p5, 5); WL (p5, 5); WL (p5, 5); WL (p5, 5); WL (p5, 5);
  WL (p5, 5); WL (p5, 5); WL (p5, 5); WL (p5, 5); WL (p5, 5);

  ST (h, 336); ST (h, 344); ST (h, 352); ST (h, 360);
  ST (h, 368); ST (h, 376); ST (h, 384); ST (h, 392);
  ST (h1, 400); ST (h1, 408); ST (h1, 416); ST (h1, 424);
  ST (h1, 432); ST (h1, 440); ST (h1, 448); ST (h1, 456);
  ST (h2, 464); ST (h2, 472); ST (h2, 480); ST (h2, 488);
  ST (h2, 496); ST (h2, 504); ST (h2, 512);
}
