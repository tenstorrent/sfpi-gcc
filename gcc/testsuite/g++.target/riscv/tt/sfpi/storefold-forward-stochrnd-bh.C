// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-store-fold -fdump-tree-rvtt_store_fold" }
// The all-lanes merge the vFloat16b convert wrapper emits forwards into
// the store (S1, byte-neutral after coalescing), and the exposed
// SFPSTOCHRND-into-store candidate carries its standing named refusal:
// the store's BF16 conversion truncates toward zero and preserves
// -0/denormal signs while SFPSTOCHRND rounds to nearest-ties-away and
// normalizes specials -- tt/proofs/stochrnd-store-round/ (NOT-EQUAL,
// 2,155,741,184 / 2^32 on the BF16 row).  The explicit rounding word is
// SEMANTICS, not a delivery artifact; it must stay.
// { dg-final { scan-tree-dump-times "store-fold: forwarded merge source" 1 "rvtt_store_fold" } }
// { dg-final { scan-tree-dump "store-fold refused .stochrnd-store-rounding-divergent" "rvtt_store_fold" } }
// { dg-final { scan-assembler "SFPSTOCHRND" } }
extern volatile unsigned __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>
__attribute__((noinline)) void
storefold_stochrnd ()
{
  for (int ix = 0; ix < 8; ++ix)
    {
      const sfpi::vFloat a = sfpi::dst_reg[0];
      const sfpi::vFloat b = sfpi::dst_reg[32];
      sfpi::vFloat r = a - b;
      r = sfpi::convert<sfpi::vFloat16b>(r, sfpi::RoundMode::Nearest);
      sfpi::dst_reg[0] = r;
      sfpi::dst_reg++;
    }
}
