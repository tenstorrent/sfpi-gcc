// { dg-options "-mcpu=tt-qsr32-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }
// Quasar does not expose the six-register SFPLUTFP32 builtin.  Keep this
// target-capability diagnostic explicit rather than expecting the BH/WH
// builtin's mod-mask diagnostic from a builtin that does not exist here.

extern volatile unsigned long __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned long (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>

using namespace sfpi;

__attribute__((noinline)) void
lut2_fp32_qsr_refused ()
{
  vFloat a0 = dst_reg[1], a1 = dst_reg[2], a2 = dst_reg[3];
  vFloat b0 = dst_reg[4], b1 = dst_reg[5], b2 = dst_reg[6];
  vFloat v = dst_reg[0];
  dst_reg[0] = vFloat (__builtin_rvtt_sfplutfp32_6r // { dg-error "was not declared in this scope" }
			       (a0.get (), a1.get (), a2.get (),
				b0.get (), b1.get (), b2.get (),
				v.get (), 0));
}
