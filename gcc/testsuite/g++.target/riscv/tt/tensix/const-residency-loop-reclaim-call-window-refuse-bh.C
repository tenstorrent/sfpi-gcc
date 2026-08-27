// LOOP-RECLAIM window refuse twin (lane ID): the loop-class hoist
// admission already excludes foreign calls and asm from the loop body
// (opaque-hoist-region), so the reachable unproven-window shape is a
// LIFTED programming point -- under -mtt-tensix-optimize-crossloop-hoist
// the candidate's entry edge is the outermost proven entry, and the
// window from there to the inner loop's readers spans enclosing-loop
// content this walk does not prove.  The DEAD-claim reclaim refuses by
// name; every claim is dead, nothing is reclaimed, and the established
// prgm-exhausted placement is kept.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-crossloop-hoist -mtt-tensix-optimize-loop-prgm-reclaim -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump "loop-reclaim-call-window" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "reclaiming DEAD-claimed" "rvtt_prgm_const" } }

void owner_init (void)
{
  auto c0 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e317218, 0, 0, 31);
  __builtin_rvtt_sfpwriteconfig_v (c0, 12);
  auto k1 = __builtin_rvtt_sfpxloadi (nullptr, 0x3dd8adac, 0, 0, 31);
  __builtin_rvtt_sfpwriteconfig_v (k1, 13);
  auto k2 = __builtin_rvtt_sfpxloadi (nullptr, 0xbf317218, 0, 0, 31);
  __builtin_rvtt_sfpwriteconfig_v (k2, 14);
}

void kernel_nested (void)
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  for (unsigned oy = 0; oy != 4; ++oy)
    for (unsigned ix = 0; ix != 32; ++ix)
      {
	auto h1 = __builtin_rvtt_sfpxloadi (nullptr, 0x3f000000, 0, 0, 31);
	auto pi = __builtin_rvtt_sfpxloadi (nullptr, 0x40490fdb, 0, 0, 31);
	x = __builtin_rvtt_sfpmul (x, h1, 0);
	x = __builtin_rvtt_sfpadd (x, pi, 0);
      }
  __builtin_rvtt_sfpwritelreg (x, 0);
}
