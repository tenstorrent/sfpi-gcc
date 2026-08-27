// LOOP-RECLAIM off-twin (lane ID): the fire twin's exact source
// WITHOUT -mtt-tensix-optimize-loop-prgm-reclaim keeps the established
// placement byte-identically -- the TU value-identical reuse still
// fires (shipped tier), every other value refuses prgm-exhausted, and
// no DEAD-claim reclaim happens.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-times "reusing TU-programmed PRGM L12 .every TU write stores 0x3e317218" 1 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "reclaiming DEAD-claimed" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "loop-reclaim-call-window" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "refused .prgm-exhausted" 4 "rvtt_prgm_const" } }
// The init's three SFPCONFIGs plus the value-identical pair's two
// programmings (the duplicate's reprogram is the shipped
// redundant-but-sound path).
// { dg-final { scan-assembler-times "SFPCONFIG" 5 } }

void owner_init (void)
{
  auto c0 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e317218, 0, 0, 31);
  __builtin_rvtt_sfpwriteconfig_v (c0, 12);
  auto k1 = __builtin_rvtt_sfpxloadi (nullptr, 0x3dd8adac, 0, 0, 31);
  __builtin_rvtt_sfpwriteconfig_v (k1, 13);
  auto k2 = __builtin_rvtt_sfpxloadi (nullptr, 0xbf317218, 0, 0, 31);
  __builtin_rvtt_sfpwriteconfig_v (k2, 14);
}

void kernel_inloop (void)
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto g1 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e317218, 0, 0, 31);
      auto g2 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e317218, 0, 0, 31);
      auto h1 = __builtin_rvtt_sfpxloadi (nullptr, 0x3f000000, 0, 0, 31);
      auto h2 = __builtin_rvtt_sfpxloadi (nullptr, 0x3f000000, 0, 0, 31);
      auto pi = __builtin_rvtt_sfpxloadi (nullptr, 0x40490fdb, 0, 0, 31);
      auto fv = __builtin_rvtt_sfpxloadi (nullptr, 0x40b00000, 0, 0, 31);
      x = __builtin_rvtt_sfpmul (x, g1, 0);
      x = __builtin_rvtt_sfpadd (x, g2, 0);
      x = __builtin_rvtt_sfpmul (x, h1, 0);
      x = __builtin_rvtt_sfpadd (x, h2, 0);
      x = __builtin_rvtt_sfpmul (x, pi, 0);
      x = __builtin_rvtt_sfpadd (x, fv, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}
