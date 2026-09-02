// OPAQUE-REPLAY-RECORD default-off: the same TU as the fire
// twin WITHOUT -mtt-tensix-optimize-opaque-replay-record keeps the
// established opaque-region refusal byte-identically -- the record
// word refuses through the audited table, no region machinery runs,
// no residency placement fires.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-pressure-park -mtt-tensix-optimize-crossloop-hoist -mtt-tensix-optimize-crossloop-cc-peel -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump "unaudited raw opcode" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump "refused .opaque-region-undeclared." "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "replay record word admitted" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "record-window word swallowed" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "allocated PRGM" "rvtt_prgm_const" } }
// { dg-final { scan-assembler-not "SFPCONFIG" } }

static void record_region_init ()
{
  __asm__ __volatile__ (".ttinsn %0" : : "n" (0x040000E1)); // REPLAY(0,14,0,1)
  __asm__ __volatile__ (".ttinsn %0" : : "n" (0x7D000020));
  __asm__ __volatile__ (".ttinsn %0" : : "n" (0x7E000200));
  __asm__ __volatile__ (".ttinsn %0" : : "n" (0x81000002));
  __asm__ __volatile__ (".ttinsn %0" : : "n" (0x79000304));
  __asm__ __volatile__ (".ttinsn %0" : : "n" (0x94002005));
  __asm__ __volatile__ (".ttinsn %0" : : "n" (0x92000011));
  __asm__ __volatile__ (".ttinsn %0" : : "n" (0x79000106));
  __asm__ __volatile__ (".ttinsn %0" : : "n" (0x7D000020));
  __asm__ __volatile__ (".ttinsn %0" : : "n" (0x7E000200));
  __asm__ __volatile__ (".ttinsn %0" : : "n" (0x81000002));
  __asm__ __volatile__ (".ttinsn %0" : : "n" (0x79000304));
  __asm__ __volatile__ (".ttinsn %0" : : "n" (0x94002005));
  __asm__ __volatile__ (".ttinsn %0" : : "n" (0x92000011));
  __asm__ __volatile__ (".ttinsn %0" : : "n" (0x79000106));
}

void orr_default_off (int tiles)
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
