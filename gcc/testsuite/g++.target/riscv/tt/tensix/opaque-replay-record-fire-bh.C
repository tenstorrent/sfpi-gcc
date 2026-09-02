// OPAQUE-REPLAY-RECORD fire, straight-line region: a raw
// Tensix REPLAY record word (load_mode=1, exec=0) followed by its
// swallowed raw words -- the binary-GCD init idiom -- no longer
// refuses the whole TU's PRGM freedom proof (opaque-region-undeclared).
// The recorded words are architecturally never delivered (stored to
// the replay buffer, not pushed to the backend), the TU contains no
// playback path, so the region contributes no PRGM/LaneConfig/CC
// effect and the residency allocation in the compute loop fires.  The
// swallowed content includes the CC-writing SFPLZ modifier word: the
// no-playback theorem, not a per-opcode table, discharges it.  The
// twin varies names, constants, region length, and trip shapes: the
// decision must key on none of them.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-pressure-park -mtt-tensix-optimize-crossloop-hoist -mtt-tensix-optimize-crossloop-cc-peel -mtt-tensix-optimize-opaque-replay-record -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-times "replay record word admitted .no playback path." 2 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "record-window word swallowed" 21 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "opaque-region-undeclared" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "unaudited raw opcode" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "allocated PRGM L1\\d for constant 0x\[0-9a-f\]+ .loop class" 2 "rvtt_prgm_const" } }
// { dg-final { scan-assembler-times "SFPCONFIG" 2 } }

static void record_region_init ()
{
  __asm__ __volatile__ (".ttinsn %0" : : "n" (0x040000E1)); // REPLAY(0,14,0,1)
  __asm__ __volatile__ (".ttinsn %0" : : "n" (0x7D000020)); // SFPABS
  __asm__ __volatile__ (".ttinsn %0" : : "n" (0x7E000200)); // SFPAND
  __asm__ __volatile__ (".ttinsn %0" : : "n" (0x81000002)); // SFPLZ CC_NE0
  __asm__ __volatile__ (".ttinsn %0" : : "n" (0x79000304)); // SFPIADD
  __asm__ __volatile__ (".ttinsn %0" : : "n" (0x94002005)); // SFPSHFT2
  __asm__ __volatile__ (".ttinsn %0" : : "n" (0x92000011)); // SFPSWAP
  __asm__ __volatile__ (".ttinsn %0" : : "n" (0x79000106)); // SFPIADD
  __asm__ __volatile__ (".ttinsn %0" : : "n" (0x7D000020));
  __asm__ __volatile__ (".ttinsn %0" : : "n" (0x7E000200));
  __asm__ __volatile__ (".ttinsn %0" : : "n" (0x81000002));
  __asm__ __volatile__ (".ttinsn %0" : : "n" (0x79000304));
  __asm__ __volatile__ (".ttinsn %0" : : "n" (0x94002005));
  __asm__ __volatile__ (".ttinsn %0" : : "n" (0x92000011));
  __asm__ __volatile__ (".ttinsn %0" : : "n" (0x79000106));
}

void orr_fire (int tiles)
{
  record_region_init ();
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

// Renamed/varied sibling: a 7-word window, different constant, other
// trip counts.
static void varied_window_setup ()
{
  __asm__ __volatile__ (".ttinsn %0" : : "n" (0x04000071)); // REPLAY(0,7,0,1)
  __asm__ __volatile__ (".ttinsn %0" : : "n" (0x92000011));
  __asm__ __volatile__ (".ttinsn %0" : : "n" (0x79000106));
  __asm__ __volatile__ (".ttinsn %0" : : "n" (0x7D000020));
  __asm__ __volatile__ (".ttinsn %0" : : "n" (0x81000002));
  __asm__ __volatile__ (".ttinsn %0" : : "n" (0x7E000200));
  __asm__ __volatile__ (".ttinsn %0" : : "n" (0x94002005));
  __asm__ __volatile__ (".ttinsn %0" : : "n" (0x79000304));
}

void renamed_varied_scale (int faces)
{
  varied_window_setup ();
  for (int face = 0; face != faces; ++face)
    {
      __asm__ __volatile__ (".ttinsn %0" : : "n" (0x37000104));
      for (int lane = 0; lane != 12; ++lane)
	{
	  auto west = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);
	  __builtin_rvtt_sfppushc (0);
	  __builtin_rvtt_sfpsetcc_v (west, 0);
	  __builtin_rvtt_sfppopc (0);
	  auto bias = __builtin_rvtt_sfpxloadi (nullptr, 0x40e90fdb,
						0, 0, 31);
	  west = __builtin_rvtt_sfpmul (west, bias, 0);
	  __builtin_rvtt_sfpstore (nullptr, west, 0, 0, 0, 6, 7);
	  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
	}
    }
}
