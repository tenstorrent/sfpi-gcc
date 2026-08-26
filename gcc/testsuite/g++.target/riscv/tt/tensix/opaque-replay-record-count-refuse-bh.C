// OPAQUE-REPLAY-RECORD near miss, partial-trip swallow (lane HS): the
// record window (21) does not cover the counted loop's full delivery
// (4 x 7 = 28) -- some executions of the same statements would be
// swallowed and others delivered, which cannot be attributed per
// trip.  The window refuses by name; the TU keeps its opaque refusal.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-pressure-park -mtt-tensix-optimize-crossloop-hoist -mtt-tensix-optimize-crossloop-cc-peel -mtt-tensix-optimize-opaque-replay-record -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump "replay-record-count-unproven" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump "refused .opaque-region-undeclared." "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "allocated PRGM" "rvtt_prgm_const" } }
// { dg-final { scan-assembler-not "SFPCONFIG" } }

static void short_window_over_loop ()
{
  __asm__ __volatile__ (".ttinsn %0" : : "n" (0x04000151)); // REPLAY(0,21,0,1)
#pragma GCC unroll 1
  for (int i = 0; i < 4; ++i)
    {
      __asm__ __volatile__ (".ttinsn %0" : : "n" (0x7D000020));
      __asm__ __volatile__ (".ttinsn %0" : : "n" (0x7E000200));
      __asm__ __volatile__ (".ttinsn %0" : : "n" (0x81000002));
      __asm__ __volatile__ (".ttinsn %0" : : "n" (0x79000304));
      __asm__ __volatile__ (".ttinsn %0" : : "n" (0x94002005));
      __asm__ __volatile__ (".ttinsn %0" : : "n" (0x92000011));
      __asm__ __volatile__ (".ttinsn %0" : : "n" (0x79000106));
    }
}

void orr_count_refuse (int tiles)
{
  short_window_over_loop ();
  for (int t = 0; t != tiles; ++t)
    {
      __asm__ __volatile__ (".ttinsn %0" : : "n" (0x37000104));
      for (int row = 0; row != 8; ++row)
	{
	  auto x = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);
	  __builtin_rvtt_sfppushc (0);
	  __builtin_rvtt_sfpsetcc_v (x, 0);
	  __builtin_rvtt_sfppopc (0);
	  auto gain = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b1a3d,
						0, 0, 31);
	  x = __builtin_rvtt_sfpmul (x, gain, 0);
	  __builtin_rvtt_sfpstore (nullptr, x, 0, 0, 0, 6, 7);
	  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
	}
    }
}
