// OPAQUE-REPLAY-RECORD near miss, playback word: a raw
// REPLAY execute (load_mode=0) delivers recorded buffer content --
// auditing playback is a later increment, and admitting records is
// SOUND ONLY BECAUSE executes refuse (the no-playback theorem's arm
// (a)).  The TU keeps its opaque refusal by the playback's name.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-pressure-park -mtt-tensix-optimize-crossloop-hoist -mtt-tensix-optimize-crossloop-cc-peel -mtt-tensix-optimize-opaque-replay-record -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump "replay-execute-unproven: playback of recorded content" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump "refused .opaque-region-undeclared." "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "allocated PRGM" "rvtt_prgm_const" } }
// { dg-final { scan-assembler-not "SFPCONFIG" } }

static void window_and_playback ()
{
  __asm__ __volatile__ (".ttinsn %0" : : "n" (0x04000021)); // REPLAY(0,2,0,1)
  __asm__ __volatile__ (".ttinsn %0" : : "n" (0x7D000020)); // SFPABS
  __asm__ __volatile__ (".ttinsn %0" : : "n" (0x7E000200)); // SFPAND
  __asm__ __volatile__ (".ttinsn %0" : : "n" (0x04000020)); // REPLAY(0,2,0,0) execute
}

void orr_execute_refuse (int tiles)
{
  window_and_playback ();
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
