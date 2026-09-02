// OPAQUE-REPLAY-RECORD near miss, neutered nested window: a
// REPLAY word arriving inside an OPEN window is stored as data by the
// hardware (the window arm precedes the REPLAY decode arm) -- it never
// acts.  Here an exec-while-loading record's window contains a nested
// exec=0 record that would otherwise claim to swallow the CC-writing
// words after it; treating the neutered word as acting would silently
// drop those words from the executed-word census.  The outer window's
// walk refuses the nested expander word by name; the TU keeps its
// opaque refusal.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-pressure-park -mtt-tensix-optimize-crossloop-hoist -mtt-tensix-optimize-crossloop-cc-peel -mtt-tensix-optimize-opaque-replay-record -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump "replay-record-nested-unproven: expander word in recorded content" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump "refused .opaque-region-undeclared." "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "allocated PRGM" "rvtt_prgm_const" } }
// { dg-final { scan-assembler-not "SFPCONFIG" } }

static void neutered_nested_window ()
{
  __asm__ __volatile__ (".ttinsn %0" : : "n" (0x04000023)); // REPLAY(0,2,1,1) exec-record
  __asm__ __volatile__ (".ttinsn %0" : : "n" (0x37000104)); // SETRWC
  __asm__ __volatile__ (".ttinsn %0" : : "n" (0x04000021)); // REPLAY(0,2,0,1) -- NEUTERED to data
  __asm__ __volatile__ (".ttinsn %0" : : "n" (0x81000002)); // SFPLZ CC_NE0 (would be mis-suppressed)
  __asm__ __volatile__ (".ttinsn %0" : : "n" (0x7D000020)); // SFPABS
}

void orr_nested_refuse (int tiles)
{
  neutered_nested_window ();
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
