// A sign-ignoring leading-zero count (SFPLZ mode 4) writes no lane flags,
// so it is legal outside any v_if and needs no CC bookkeeping.
// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }
// { dg-final { scan-assembler-times "SFPLZ\tL\[0-7\], L\[0-7\], 4" 1 } }
// { dg-final { scan-assembler-not "SFPENCC" } }

extern volatile unsigned long __instrn_buffer[];
namespace ckernel
{
constexpr inline volatile unsigned long (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>

void
magnitude_lz ()
{
  const sfpi::vFloat v = sfpi::dst_reg[0];
  sfpi::dst_reg[0] = sfpi::convert<sfpi::vFloat> (
    sfpi::lz (sfpi::as<sfpi::vUInt> (v), sfpi::LZMode::IgnoreSign),
    sfpi::RoundMode::Nearest);
}
