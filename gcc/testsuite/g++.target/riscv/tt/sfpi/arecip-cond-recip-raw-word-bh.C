// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }
// GS-3 unlock: approx_recip(src, RecipMode::IfNegative) needs VB == VC
// (recip where the SOURCE is negative), but the gas mnemonic forces the
// imm12 (VB) field to zero -- the lowering emits the architecturally
// encoded raw word instead (op 0x99, VB == VC in imm12's low nibble,
// Mod1 = 1), annotated with its decode.
// { dg-final { scan-assembler "\\.ttinsn\t\[0-9\]+\t# SFPARECIP" } }
// { dg-final { scan-assembler-not "\n\tSFPARECIP\tL\[0-7\], L\[0-7\], 1" } }
extern volatile unsigned __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>

__attribute__((noinline)) void
arecip_cond_raw_word ()
{
  sfpi::vFloat x = sfpi::dst_reg[0];
  sfpi::dst_reg[0] = sfpi::approx_recip (x, sfpi::RecipMode::IfNegative);
}
