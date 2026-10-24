// Check that a raw REPLAY record whose words are never played back does not refuse the TU's PRGM freedom proof.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-pressure-park -mtt-tensix-optimize-crossloop-hoist -mtt-tensix-optimize-crossloop-cc-peel -mtt-tensix-optimize-opaque-replay-record -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-times "replay record word admitted .no playback path." 2 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "record-window word swallowed" 21 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "opaque-region-undeclared" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "unaudited raw opcode" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "allocated PRGM L1\\d for constant 0x\[0-9a-f\]+ .loop class" 2 "rvtt_prgm_const" } }
// { dg-final { scan-assembler-times "SFPCONFIG" 4 } }

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
  // Reset only after the complete owned recording window.
  __builtin_rvtt_sfpencc_all_lanes ();
  __builtin_rvtt_sfpconfig_i (0, 15, 1);
  for (int t = 0; t != tiles; ++t)
    {
      __asm__ __volatile__ (".ttinsn %0" : : "n" (0x37000104));
      for (int row = 0; row != 8; ++row)
	{
	  auto x = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);
	  __builtin_rvtt_sfppushc (0);
	  __builtin_rvtt_sfpsetcc (x, 0);
	  __builtin_rvtt_sfppopc (0);
	  auto gain = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b1a3d,
						0, 0, 31);
	  x = __builtin_rvtt_sfpmul (x, gain, 0);
	  __builtin_rvtt_sfpstore (nullptr, x, 0, 0, 0, 6, 7);
	  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
	}
    }
}

// Same shape with a 7-word window, a different constant and other trip counts.
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
  // Reset only after the complete owned recording window.
  __builtin_rvtt_sfpencc_all_lanes ();
  __builtin_rvtt_sfpconfig_i (0, 15, 1);
  for (int face = 0; face != faces; ++face)
    {
      __asm__ __volatile__ (".ttinsn %0" : : "n" (0x37000104));
      for (int lane = 0; lane != 12; ++lane)
	{
	  auto west = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);
	  __builtin_rvtt_sfppushc (0);
	  __builtin_rvtt_sfpsetcc (west, 0);
	  __builtin_rvtt_sfppopc (0);
	  auto bias = __builtin_rvtt_sfpxloadi (nullptr, 0x40e90fdb,
						0, 0, 31);
	  west = __builtin_rvtt_sfpmul (west, bias, 0);
	  __builtin_rvtt_sfpstore (nullptr, west, 0, 0, 0, 6, 7);
	  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
	}
    }
}
