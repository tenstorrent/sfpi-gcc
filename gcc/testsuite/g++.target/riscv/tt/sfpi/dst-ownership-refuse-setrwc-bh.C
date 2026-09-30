// Track B near-miss: a typed TTSETRWC between the load and the reload
// rewrites the RWC counters; the face-identity chain splits and the
// pair must refuse.
// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-dst-ownership -fdump-rtl-rvtt_dst_ownership-details" }
// { dg-final { scan-rtl-dump "dst-rwc-effect-unproved .rwc-boundary." "rvtt_dst_ownership" } }
// { dg-final { scan-rtl-dump-not "Dst-ownership fold" "rvtt_dst_ownership" } }
// { dg-final { scan-assembler-times {\mSFPLOAD\t} 2 } }

#include <cstdint>
#include <lltt.h>

namespace ckernel {
constexpr inline volatile uint32_t (&instrn_buffer)[] = ::__instrn_buffer;
}

#include <sfpi.h>

using namespace sfpi;

__attribute__((noinline)) void
setrwc_between ()
{
  vFloat in = dst_reg[0];
  vFloat r = in + 1.0f;
  lltt::setrwc<0, 4, 8, 0, 0, 4> ();	// typed counter write
  vFloat again = dst_reg[0];
  dst_reg[0] = r * again;
}
