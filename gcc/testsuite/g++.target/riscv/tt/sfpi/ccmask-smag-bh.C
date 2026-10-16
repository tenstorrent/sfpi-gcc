// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-ccmask -fdump-tree-rvtt_ccmask" }
// Check that sign-magnitude to two's-complement conversions fold to SFPABS, SFPXOR and SFPSETSGN.
// { dg-final { scan-tree-dump-times "ccmask: folded sign-magnitude conversion" 2 "rvtt_ccmask" } }
// { dg-final { scan-assembler-times "SFPABS\tL\[0-7\], L\[0-7\], 0" 2 } }
// { dg-final { scan-assembler-times "SFPXOR\tL\[0-7\], L\[0-7\]" 2 } }
// { dg-final { scan-assembler-times "SFPSETSGN\tL\[0-7\], L\[0-7\], 0, 0" 2 } }
// { dg-final { scan-assembler-times "SFPCAST\tL\[0-7\], L\[0-7\], 3" 1 } }
// { dg-final { scan-assembler-not "SFPSETCC" } }
// { dg-final { scan-assembler-not "SFPENCC" } }
extern volatile unsigned long __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned long (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>
__attribute__((noinline)) void
ccmask_smag_sub ()
{
  sfpi::vInt a = sfpi::dst_reg[0].mode<sfpi::DataLayout::SM32> ();
  sfpi::vInt b = sfpi::dst_reg[32].mode<sfpi::DataLayout::SM32> ();
  sfpi::dst_reg[0].mode<sfpi::DataLayout::SM32> () = a - b;
}
