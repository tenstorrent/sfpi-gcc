// WH arm of opaque-replay-record-fire-bh.C: the record-window theorem
// is arch-independent (the replay expander model and field decode are
// the same on WH; [SIM] facts in rvtt-mop-tables.h cover both).  One
// function (the BH file carries the renamed twin pair).
// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-pressure-park -mtt-tensix-optimize-crossloop-hoist -mtt-tensix-optimize-crossloop-cc-peel -mtt-tensix-optimize-opaque-replay-record -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-times "replay record word admitted .no playback path." 1 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "record-window word swallowed" 7 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "opaque-region-undeclared" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "allocated PRGM L1\\d for constant 0x\[0-9a-f\]+ .loop class" 1 "rvtt_prgm_const" } }
// { dg-final { scan-assembler-times "SFPCONFIG" 1 } }

static void record_region_init_wh ()
{
  __asm__ __volatile__ (".ttinsn %0" : : "n" (0x04000071)); // REPLAY(0,7,0,1)
  __asm__ __volatile__ (".ttinsn %0" : : "n" (0x7D000020));
  __asm__ __volatile__ (".ttinsn %0" : : "n" (0x7E000200));
  __asm__ __volatile__ (".ttinsn %0" : : "n" (0x81000002));
  __asm__ __volatile__ (".ttinsn %0" : : "n" (0x79000304));
  __asm__ __volatile__ (".ttinsn %0" : : "n" (0x94002005));
  __asm__ __volatile__ (".ttinsn %0" : : "n" (0x92000011));
  __asm__ __volatile__ (".ttinsn %0" : : "n" (0x79000106));
}

void orr_fire_wh (int tiles)
{
  record_region_init_wh ();
  for (int t = 0; t != tiles; ++t)
    {
      __asm__ __volatile__ (".ttinsn %0" : : "n" (0x37000104));
      for (int row = 0; row != 8; ++row)
	{
	  auto x = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 3);
	  __builtin_rvtt_sfppushc (0);
	  __builtin_rvtt_sfpsetcc_v (x, 0);
	  __builtin_rvtt_sfppopc (0);
	  auto gain = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b1a3d,
						0, 0, 31);
	  x = __builtin_rvtt_sfpmul (x, gain, 0);
	  __builtin_rvtt_sfpstore (nullptr, x, 0, 0, 0, 6, 3);
	  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
	}
    }
}
