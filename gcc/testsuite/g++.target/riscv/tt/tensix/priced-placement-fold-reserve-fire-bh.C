// PLACEMENT-ARBITER fold-reserve fire (FABLE_GOES_BURR.md item #13;
// base = priced-placement-fold-reserve-base-bh.C, same body): the
// erfinv relief lever, "price the dst-ownership fold through the
// pressure-park tier" (the pin-48 named successor; laneJT structurally
// refuted post-alloc coalescing as the alternative relief).  The
// function's last free LREG is contested between the walk's marginal
// park (k: one word saved per trip, one paid at entry -> bid
// 100*(16-1)) and the identity-reload fold demand (one delivered
// SFPLOAD word per trip -> bid 100*16).  The fold outbids by exactly
// the entry word; the marginal park yields by name
// (place-fold-reserve-outbid), the span stays at eight registers, and
// the downstream dst-ownership fold -- whose proofs are untouched --
// fires where the base leg refuses lreg-pressure-exceeded (9 > 8).
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-pressure-park -mtt-tensix-optimize-priced-placement -fdump-tree-rvtt_prgm_const-details -fdump-rtl-rvtt_dst_ownership" }
// { dg-final { scan-tree-dump "fold reserve outbids .tier-conflict place-fold-reserve-outbid." "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump "park-tier: refused .place-fold-reserve-outbid." "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "allocated PRGM L1\\d for constant 0x\[0-9a-f\]+ .loop class" 3 "rvtt_prgm_const" } }
// { dg-final { scan-rtl-dump "1 reload.s. folded" "rvtt_dst_ownership" } }
// { dg-final { scan-rtl-dump-not "lreg-pressure-exceeded" "rvtt_dst_ownership" } }

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

      /* Span ballast: six values live across the record..reload span
	 plus the xor chain; with k parked the span holds eight live
	 registers and the fold's extension is the ninth.  */
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
