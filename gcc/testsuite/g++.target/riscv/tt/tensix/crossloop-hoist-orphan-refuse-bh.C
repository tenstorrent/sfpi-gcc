// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-unroll-loops -mtt-tensix-optimize-invariant-loadi -mtt-tensix-optimize-crossloop-hoist -fdump-tree-rvtt_crossloop" }
// Fail-closed twin (from an adversarial audit):
// crossloop consumed the census verdict but never checked that the
// function it is EDITING is inside the rooted closure.  Here the in-TU
// `_start' anchor pins the external surface, so the public kernel no
// live code calls is an orphan the census SKIPS entirely (its body was
// never audited) -- yet the earlier crossloop hoisted inside it
// (verified: 2 fires on the installed binary), converting the
// extern-fixed-surface axiom into a wrong-code exposure on
// naked-asm-entry TUs.  Editing an unaudited body must refuse by name.
// { dg-final { scan-tree-dump "refused .crossloop-caller-unrooted." "rvtt_crossloop" } }
// { dg-final { scan-tree-dump-not "hoisted across" "rvtt_crossloop" } }

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

// The anchor: with an in-TU `_start' the reset vector is the image's
// only external entry, so the uncalled public kernel above is outside
// the rooted closure (an orphaned body under the census model).
extern "C" void _start () { }
