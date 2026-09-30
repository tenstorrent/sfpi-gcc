// A lowered two-word immediate is atomic.  When its SFPLOADI root is also
// consumed directly and its SFPLOADI_LV tail has multiple consumers, neither
// half may be discovered, parked, or deleted independently.  Run under the
// full checking belt so stale scalar/virtual SSA after the refusal is fatal.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -fchecking=2 -mtt-tensix-optimize-invariant-loadi -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-pressure-park -mtt-tensix-optimize-crossloop-hoist -fdump-tree-rvtt_invariant-details -fdump-tree-rvtt_prgm_const-details -fdump-tree-rvtt_crossloop-details" }
// { dg-final { scan-tree-dump-not "Invariant load candidate" "rvtt_invariant" } }
// { dg-final { scan-tree-dump-not "allocated PRGM" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "hoisted across" "rvtt_crossloop" } }
// { dg-final { scan-assembler-not "SFPCONFIG" } }
// { dg-final { scan-assembler-times "SFPLOADI" 2 } }

void
shared_root_and_tail (unsigned tiles)
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  for (unsigned tile = 0; tile != tiles; ++tile)
    {
      for (unsigned i = 0; i != 32; ++i)
	{
	  auto root = __builtin_rvtt_sfploadi (nullptr, 0x1234, 0, 0, 2);
	  auto tail = __builtin_rvtt_sfploadi_lv (nullptr, root, 0x3f80,
						  0, 0, 8);
	  x = __builtin_rvtt_sfpadd (x, root, 0);
	  x = __builtin_rvtt_sfpadd (x, tail, 0);
	  x = __builtin_rvtt_sfpmul (x, tail, 0);
	}
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}
