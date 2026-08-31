// PLACEMENT-ARBITER fold-reserve base leg (item #13; fire =
// priced-placement-fold-reserve-fire-bh.C, same body, flag on): with
// -mtt-tensix-optimize-priced-placement ABSENT the arbiter runs in
// shadow only -- it dumps the bid comparison (the fold demand outbids
// the marginal park) and changes NOTHING: the established pressure-park
// LREG hoist proceeds, the parked constant web rides through the
// record..reload span, and the dst-ownership fold refuses
// lreg-pressure-exceeded (pressure 9 > budget 8) exactly as erfinv does
// at ON (conf pin-48 witness anatomy).  The relief census names the
// parked web as the remat-class occupant -- the arbiter's stage-A
// proof artifact.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-pressure-park -fdump-tree-rvtt_prgm_const-details -fdump-rtl-rvtt_dst_ownership" }
// { dg-final { scan-tree-dump "fold demand outbids .shadow; the established park stands." "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump "hoisted invariant materialization to a free LREG" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump "residency-rank loop-class legacy=.0x3f21aa52,0x402df854,0x40490fdb,0x3e4b1a3d. priced=.0x3f21aa52,0x402df854,0x40490fdb,0x3e4b1a3d. AGREE .deciding=legacy." "rvtt_prgm_const" } }
// { dg-final { scan-rtl-dump "lreg-pressure-exceeded .pressure 9 > budget 8." "rvtt_dst_ownership" } }
// { dg-final { scan-rtl-dump "relief census at insn \\d+: r\\d+.loadi 0x\[0-9a-f\]+. -- 1 remat-class constant web.s. live through the refused span" "rvtt_dst_ownership" } }
// { dg-final { scan-rtl-dump "0 reload.s. folded" "rvtt_dst_ownership" } }

#define ST(x, r) __builtin_rvtt_sfpstore (nullptr, (x), (r), 0, 0, 4, 7)
#define LD(r) __builtin_rvtt_sfpload (nullptr, (r), 0, 0, 4, 7)

void fold_reserve_fire (void)
{
  for (unsigned ix = 0; ix != 16; ++ix)
    {
      auto k  = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b1a3d, 0, 0, 31);
      ST (k, 208);
      auto c0 = __builtin_rvtt_sfpxloadi (nullptr, 0x3f21aa52, 0, 0, 31);
      ST (c0, 216); ST (c0, 224);
      auto c1 = __builtin_rvtt_sfpxloadi (nullptr, 0x40490fdb, 0, 0, 31);
      ST (c1, 232); ST (c1, 240);
      auto c2 = __builtin_rvtt_sfpxloadi (nullptr, 0x402df854, 0, 0, 31);
      ST (c2, 248); ST (c2, 256);

      auto b1 = LD (384);
      auto b2 = LD (392);
      auto b3 = LD (400);
      auto b4 = LD (408);
      auto b5 = LD (416);
      auto v  = LD (320);	/* record */
      auto s  = __builtin_rvtt_sfpxor (v, v);
      auto b6 = LD (424);
      auto t  = __builtin_rvtt_sfpxor (s, b6);
      auto u  = __builtin_rvtt_sfpxor (t, b5);
      auto w  = LD (320);	/* identity reload */
      ST (w, 328); ST (u, 336);
      ST (b1, 344); ST (b2, 352); ST (b3, 360); ST (b4, 368);
    }
}
