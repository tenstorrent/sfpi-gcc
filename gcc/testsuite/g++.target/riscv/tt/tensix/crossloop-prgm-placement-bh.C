// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-unroll-loops -mtt-tensix-optimize-prgm-const -mtt-tensix-optimize-crossloop-hoist -fdump-tree-rvtt_prgm_const" }
// Programmable-constant placement under the cross-loop hoist: the
// fusion class allocates at the row-loop entry edge, and the audited
// outward walk lifts the programming point across the tile loop.
// { dg-final { scan-tree-dump "allocated PRGM L" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump "crossloop-hoist: placement lifted from entry bb" "rvtt_prgm_const" } }

extern volatile unsigned int __instrn_buffer[];

void
xlh_prgm_kernel (int tiles)
{
  for (int t = 0; t != tiles; ++t)
    {
      __asm__ __volatile__ (".ttinsn %0" : : "n" (0x37000104));
      __asm__ __volatile__ (".ttinsn %0" : : "n" (0xa2820010));
      auto x = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);
      for (int row = 0; row != 32; ++row)
	{
	  auto prod = __builtin_rvtt_sfpmul (x, x, 0);
	  x = __builtin_rvtt_sfpaddi (nullptr, prod, 0x42fe, 0, 0, 0);
	}
      __builtin_rvtt_sfpstore (nullptr, x, 0, 0, 0, 6, 7);
    }
}
