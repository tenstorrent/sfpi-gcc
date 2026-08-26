// OPAQUE-REPLAY-RECORD fire, rolled counted-loop region (lane HS):
// the production shape -- ckernel_sfpu_gcd.h calculate_sfpu_gcd_init's
// TTI_REPLAY(0,28,0,1) followed by a 4-trip loop of 7 recorded words.
// The einline-stage body the TU walk scans is still rolled, so the
// record window's swallow proof must count the loop structurally:
// exact trips from the loop's own IV and guard, every trip swallowed
// (28 = 4 x 7 exactly).  The pragma keeps the twin's loop rolled in
// every copy so the counted arm is exercised regardless of scan
// order.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-pressure-park -mtt-tensix-optimize-crossloop-hoist -mtt-tensix-optimize-crossloop-cc-peel -mtt-tensix-optimize-opaque-replay-record -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-times "replay record word admitted .no playback path." 1 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "record-window word swallowed" 7 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "opaque-region-undeclared" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "unaudited raw opcode" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "allocated PRGM L1\\d for constant 0x\[0-9a-f\]+ .loop class" 1 "rvtt_prgm_const" } }
// { dg-final { scan-assembler-times "SFPCONFIG" 1 } }

static void rolled_record_init ()
{
  __asm__ __volatile__ (".ttinsn %0" : : "n" (0x040001C1)); // REPLAY(0,28,0,1)
#pragma GCC unroll 1
  for (int i = 0; i < 4; ++i)
    {
      __asm__ __volatile__ (".ttinsn %0" : : "n" (0x7D000020)); // SFPABS
      __asm__ __volatile__ (".ttinsn %0" : : "n" (0x7E000200)); // SFPAND
      __asm__ __volatile__ (".ttinsn %0" : : "n" (0x81000002)); // SFPLZ CC_NE0
      __asm__ __volatile__ (".ttinsn %0" : : "n" (0x79000304)); // SFPIADD
      __asm__ __volatile__ (".ttinsn %0" : : "n" (0x94002005)); // SFPSHFT2
      __asm__ __volatile__ (".ttinsn %0" : : "n" (0x92000011)); // SFPSWAP
      __asm__ __volatile__ (".ttinsn %0" : : "n" (0x79000106)); // SFPIADD
    }
}

void orr_rolled_fire (int tiles)
{
  rolled_record_init ();
  for (int t = 0; t != tiles; ++t)
    {
      __asm__ __volatile__ (".ttinsn %0" : : "n" (0x37000104));
      for (int row = 0; row != 8; ++row)
	{
	  auto x = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);
	  __builtin_rvtt_sfppushc (0);
	  __builtin_rvtt_sfpsetcc_v (x, 0);
	  __builtin_rvtt_sfppopc (0);
	  auto gain = __builtin_rvtt_sfpxloadi (nullptr, 0x3fb504f3,
						0, 0, 31);
	  x = __builtin_rvtt_sfpmul (x, gain, 0);
	  __builtin_rvtt_sfpstore (nullptr, x, 0, 0, 0, 6, 7);
	  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
	}
    }
}
