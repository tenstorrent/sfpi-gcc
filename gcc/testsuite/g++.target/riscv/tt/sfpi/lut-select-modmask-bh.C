// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }
// Capability-table correction: the architecturally defined
// FP32-3ENTRY (0) and SGN_RETAIN (+4) SFPLUTFP32 mode words are
// accepted for the six-register builtin (sfpi's own lut2 wrappers
// spell them), independent of any optimization flag.
// { dg-final { scan-assembler-times "SFPLUTFP32" 2 } }

extern volatile unsigned __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>

using namespace sfpi;

__attribute__((noinline)) void
lut2_fp32_wrappers ()
{
  vFloat a0 = dst_reg[1], a1 = dst_reg[2], a2 = dst_reg[3];
  vFloat b0 = dst_reg[4], b1 = dst_reg[5], b2 = dst_reg[6];
  vFloat v = dst_reg[0];
  dst_reg[0] = lut2 (v, a0, a1, a2, b0, b1, b2);	  // mod 4 (RETAIN)
  dst_reg[7] = vFloat (__builtin_rvtt_sfplutfp32_6r
		       (a0.get (), a1.get (), a2.get (),
			b0.get (), b1.get (), b2.get (),
			v.get (), 0));			  // mod 0 (UPDATE)
}
