// { dg-options "-mcpu=tt-qsr32-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }
// Quasar keeps the previous conservative mod set for the six-register
// SFPLUTFP32 builtin: the widened WH/BH modes stay refused there.

extern volatile unsigned __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>

using namespace sfpi;

__attribute__((noinline)) void
lut2_fp32_qsr_refused ()
{
  vFloat a0 = dst_reg[1], a1 = dst_reg[2], a2 = dst_reg[3];
  vFloat b0 = dst_reg[4], b1 = dst_reg[5], b2 = dst_reg[6];
  vFloat v = dst_reg[0];
  dst_reg[0] = vFloat (__builtin_rvtt_sfplutfp32_6r
		       (a0.get (), a1.get (), a2.get (),
			b0.get (), b1.get (), b2.get (),
			v.get (), 0));	// { dg-error "invalid mod1" }
}
