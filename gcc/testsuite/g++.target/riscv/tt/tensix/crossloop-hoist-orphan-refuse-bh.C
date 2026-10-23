// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-unroll-loops -mtt-tensix-optimize-invariant-loadi -mtt-tensix-optimize-crossloop-hoist -fdump-tree-rvtt_crossloop" }
// A genuinely private unreferenced body is dead, not another public ABI entry.
// { dg-final { scan-tree-dump-not "hoisted across" "rvtt_crossloop" } }
// { dg-final { scan-assembler-not "_Z11xlho_kerneli" } }

#ifndef XLHO_KERNEL_LINKAGE
#define XLHO_KERNEL_LINKAGE static
#endif
__attribute__((noinline)) XLHO_KERNEL_LINKAGE void
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

// An anchor does not hide other public definitions. The paired public-entry
// fixture requires actual hoisting in the otherwise identical live body.
extern "C" void _start () { }
