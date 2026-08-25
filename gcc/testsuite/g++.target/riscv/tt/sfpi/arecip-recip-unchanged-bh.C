// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }
// Mode control: the established RECIP mode (Mod1 = 0) keeps the plain
// mnemonic byte-identically -- no raw word.
// { dg-final { scan-assembler-times "SFPARECIP\tL\[0-7\], L\[0-7\], 0" 1 } }
// { dg-final { scan-assembler-not "\\.ttinsn" } }
extern volatile unsigned __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>

__attribute__((noinline)) void
arecip_recip_unchanged ()
{
  sfpi::vFloat x = sfpi::dst_reg[0];
  sfpi::dst_reg[0] = sfpi::approx_recip (x);
}
