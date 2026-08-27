// LOOP-RECLAIM (lane ID, -mtt-tensix-optimize-loop-prgm-reclaim): the
// DEAD-claim reclaim tier offered to the const-residency walk's own
// IN-LOOP candidate classes.  An init claims all three PRGM slots; no
// statement in the TU ever reads any of them (the creg_read no-reader
// proof), so every claim is DEAD.  The loop's in-loop constant
// materializations then place in the established uses-then-value
// order (selection is deliberately NOT reordered by the flag):
//  - 0x3e317218 (materialized twice, lowest value) matches the L12
//    claim bit-exactly -> the shipped TU value-identical reuse
//    (unchanged tier);
//  - 0x3f000000 (also twice) -> DEAD-claim reclaim of L13;
//  - 0x40490fdb -> DEAD-claim reclaim of L14;
//  - 0x40b00000 finds no slot and keeps the established refusal name
//    (the capacity near-miss: one more value than slots).
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-loop-prgm-reclaim -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-times "reusing TU-programmed PRGM L12 .every TU write stores 0x3e317218" 1 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "reclaiming DEAD-claimed PRGM L13 for 0x3f000000 .loop class" 1 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "reclaiming DEAD-claimed PRGM L14 for 0x40490fdb .loop class" 1 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "refused .prgm-exhausted.: _\\d+ = __builtin_rvtt_sfploadi .0B, 16560" 1 "rvtt_prgm_const" } }
// Five programming placements (the duplicate-value second statements
// re-program their shared slot: same register, same value -- the
// shipped redundant-but-sound path, and on a reclaimed slot the forced
// reprogram under the candidate's own proven window).
// { dg-final { scan-tree-dump-times "allocated PRGM L1\\d for constant 0x\[0-9a-f\]+ .loop class" 5 "rvtt_prgm_const" } }
// The init's three SFPCONFIGs plus the loop's five placements.
// { dg-final { scan-assembler-times "SFPCONFIG" 8 } }

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
