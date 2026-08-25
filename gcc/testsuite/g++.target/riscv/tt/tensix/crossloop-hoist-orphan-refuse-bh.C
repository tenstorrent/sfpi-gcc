// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-unroll-loops -mtt-tensix-optimize-invariant-loadi -mtt-tensix-optimize-crossloop-hoist -fdump-tree-rvtt_crossloop" }
// Public alternate-entry twin: the presence of an in-TU `_start' does not
// prove that it is the image's only entry.  The public kernel is therefore a
// linkage-derived census root even without an in-TU caller.  Its body is
// audited and the ordinary crossloop proof may fire; treating it as an
// unaudited orphan would reintroduce the symbol-name entry assumption.
// { dg-final { scan-tree-dump-not "refused .crossloop-caller-unrooted." "rvtt_crossloop" } }
// { dg-final { scan-tree-dump-times "hoisted across" 2 "rvtt_crossloop" } }

__attribute__((noinline)) void
xlho_kernel (int tiles)
{
  for (int t = 0; t != tiles; ++t)
    {
      __asm__ __volatile__ (".ttinsn %0" : : "n" (0x37000104));
      for (int row = 0; row != 8; ++row)
	{
	  auto c0 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b1a3d, 0, 0, -32);
	  auto c1 = __builtin_rvtt_sfpxloadi (nullptr, 0x3f2e8ba3, 0, 0, -32);
	  auto x = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);
	  x = __builtin_rvtt_sfpmad (x, c0, c1, 0);
	  __builtin_rvtt_sfpstore (nullptr, x, 0, 0, 0, 6, 7);
	  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
	}
    }
}

// A reset-vector entry does not close the image's externally callable
// surface; xlho_kernel remains a root by linkage.
extern "C" void _start () { }
