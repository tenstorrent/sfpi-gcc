// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }
// Fail-closed control: Mod1 values 3-7 (the doc's default-to-EXP
// aliases) refuse by name at the builtin check -- no encoder may rely
// on the alias (mask 0x7 = modes {0,1,2} only).
extern volatile unsigned __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>

__attribute__((noinline)) void
arecip_mod_range_refused ()
{
  sfpi::vFloat x = sfpi::dst_reg[0];
  sfpi::dst_reg[0] = sfpi::vFloat (__builtin_rvtt_sfparecip (x.get (), 3)); // { dg-error "invalid mod1 value" }
}
