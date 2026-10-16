// A sign-ignoring leading-zero count (SFPLZ mode 4) inside a v_if leaves
// the lane flags alone: it is issued between the compare that opens the
// region and the predicated store that uses it, with no SFPENCC, PUSHC or
// POPC around it, and the region is closed by its single SFPENCC.
// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }
// { dg-final { scan-assembler "SFPGT\tL\[0-9\]+, L\[0-7\], 0, 1\n\tSFPLZ\tL\[0-7\], L\[0-7\], 4\n\tSFPCAST\tL\[0-7\], L\[0-7\], 0\n\tSFPSTORE\t\[^\n\]*\n\tSFPENCC" } }
// { dg-final { scan-assembler-times "SFPENCC" 1 } }
// { dg-final { scan-assembler-not "SFPPUSHC" } }

extern volatile unsigned long __instrn_buffer[];
namespace ckernel
{
constexpr inline volatile unsigned long (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>

void
negative_lz ()
{
  const sfpi::vFloat v = sfpi::dst_reg[0];
  sfpi::dst_reg[0] = 2.0f;
  v_if (v < 0.0f)
    {
      const sfpi::vMag k = sfpi::lz (sfpi::as<sfpi::vUInt> (v), sfpi::LZMode::IgnoreSign);
      sfpi::dst_reg[0] = sfpi::convert<sfpi::vFloat> (k, sfpi::RoundMode::Nearest);
    }
  v_endif;
}
