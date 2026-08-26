// OPAQUE-REPLAY-RECORD exec-while-loading admission (lane HS): a raw
// REPLAY record with execute_while_loading=1 delivers its window
// words normally -- they are audited as executed by the ordinary scan
// (so they must be table-audited words: here SETRWC), nothing is
// suppressed, and the stored image is unreachable.  The residency
// allocation fires.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-pressure-park -mtt-tensix-optimize-crossloop-hoist -mtt-tensix-optimize-crossloop-cc-peel -mtt-tensix-optimize-opaque-replay-record -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-times "replay record word admitted .no playback path." 1 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "record-window word swallowed" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "opaque-region-undeclared" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "allocated PRGM L1\\d for constant 0x\[0-9a-f\]+ .loop class" 1 "rvtt_prgm_const" } }
// { dg-final { scan-assembler-times "SFPCONFIG" 1 } }

static void exec_record_setup ()
{
  __asm__ __volatile__ (".ttinsn %0" : : "n" (0x04000043)); // REPLAY(0,4,1,1)
  __asm__ __volatile__ (".ttinsn %0" : : "n" (0x37000104)); // SETRWC
  __asm__ __volatile__ (".ttinsn %0" : : "n" (0x37000104));
  __asm__ __volatile__ (".ttinsn %0" : : "n" (0x37000104));
  __asm__ __volatile__ (".ttinsn %0" : : "n" (0x37000104));
}

void orr_exec_while_load (int tiles)
{
  exec_record_setup ();
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
