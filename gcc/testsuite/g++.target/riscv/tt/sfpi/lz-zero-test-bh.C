// Check that lz != 32 expands to one SFPLZ in CC_NE0 mode (6) while lz == 32 keeps the subtract and SETCC.
// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }
// { dg-final { scan-assembler-times "SFPLZ\tL\[0-7\], L\[0-7\], 6" 1 } }
// { dg-final { scan-assembler-times "SFPLZ\tL\[0-7\], L\[0-7\], 4" 1 } }
// { dg-final { scan-assembler-times "SFPIADD" 1 } }
// { dg-final { scan-assembler-times "SFPSETCC" 1 } }

extern volatile unsigned long __instrn_buffer[];
namespace ckernel
{
constexpr inline volatile unsigned long (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>

void
nonzero_to_one ()
{
  const sfpi::vFloat v = sfpi::dst_reg[0];
  sfpi::dst_reg[0] = 0.0f;
  v_if (sfpi::lz (sfpi::as<sfpi::vUInt> (v), sfpi::LZMode::IgnoreSign) != 32)
    {
      sfpi::dst_reg[0] = 1.0f;
    }
  v_endif;
}

void
zero_to_one ()
{
  const sfpi::vFloat v = sfpi::dst_reg[2];
  sfpi::dst_reg[2] = 0.0f;
  v_if (sfpi::lz (sfpi::as<sfpi::vUInt> (v), sfpi::LZMode::IgnoreSign) == 32)
    {
      sfpi::dst_reg[2] = 1.0f;
    }
  v_endif;
}
