// CROSSLOOP-CC-PEEL near miss, unrelated shape 2 + the mixed
// composition: the lift requires the audited-consumer
// superset-write refinement on EVERY candidate -- including the
// pre-CC prefix, whose first-iteration lane state the forgone peel
// used to reproduce verbatim (the cc-canonical proof says nothing
// about iteration one's ambient).  Here the pre-CC candidate's value
// feeds a cross-lane SFPSHFT2 shuffle (reads lanes other than the one
// it writes, including originally-indeterminate ones): its lift
// refuses by name and it keeps the established peel placement, while
// the audited post-CC candidate of the SAME loop still lifts -- the
// peel is created for the unaudited member only, and the peel pricing
// runs over the peel members alone.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-pressure-park -mtt-tensix-optimize-crossloop-hoist -mtt-tensix-optimize-crossloop-cc-peel -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump "cc-peel lift refused .crossloop-cc-peel-consumer-unaudited" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "cc-peel placement lifted to entry bb" 1 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "peeled first iteration" 1 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "allocated PRGM L1\\d for constant 0x\[0-9a-f\]+ .loop class" 2 "rvtt_prgm_const" } }
// { dg-final { scan-assembler-times "SFPCONFIG" 2 } }

void ccpeel_consumer_refuse (int tiles)
{
  for (int t = 0; t != tiles; ++t)
    {
      __asm__ __volatile__ (".ttinsn %0" : : "n" (0x37000104));
      for (int row = 0; row != 8; ++row)
	{
	  auto x = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);
	  /* Pre-CC candidate: shuffled consumer, off the audited
	     lane-predicated set.  */
	  auto twist = __builtin_rvtt_sfpxloadi (nullptr, 0x3f2e8ba3,
						 0, 0, 31);
	  auto shuf = __builtin_rvtt_sfpshft2_subvec_shfl1 (twist, 3);
	  x = __builtin_rvtt_sfpmul (x, shuf, 0);
	  __builtin_rvtt_sfppushc (0);
	  __builtin_rvtt_sfpsetcc_v (x, 0);
	  __builtin_rvtt_sfppopc (0);
	  /* Post-CC candidate: audited consumer; lifts.  */
	  auto gain = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b1a3d,
						0, 0, 31);
	  x = __builtin_rvtt_sfpmul (x, gain, 0);
	  __builtin_rvtt_sfpstore (nullptr, x, 0, 0, 0, 6, 7);
	  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
	}
    }
}
