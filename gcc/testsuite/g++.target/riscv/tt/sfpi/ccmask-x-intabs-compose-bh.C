// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-ccmask -mtt-tensix-optimize-int-abs -fdump-tree-rvtt_ccmask -fdump-tree-rvtt_int_abs" }
// Composition twin (FH audit, FOLDS P3; zero-twin pair in the census): a
// float zeroing region and an integer conditional-negate region in ONE
// body with value coupling -- both sibling folds must fire independently.
// { dg-final { scan-tree-dump "ccmask: folded zeroing CC region" "rvtt_ccmask" } }
// { dg-final { scan-tree-dump "int-abs: folded negate-select CC region" "rvtt_int_abs" } }
extern volatile unsigned __instrn_buffer[];
namespace ckernel { constexpr inline volatile unsigned (&instrn_buffer)[] = ::__instrn_buffer; }
#include <sfpi.h>
using namespace sfpi;
__attribute__((noinline)) void
probe_pair ()
{
  for (int ix = 0; ix < 8; ++ix)
    {
      vFloat x = dst_reg[0];
      vFloat w = x * 1.25f;
      v_if (x <= 0.0f) { w = 0.0f; } v_endif;
      vInt v = as<vInt> (w);
      v_if (v < 0) { v = 0 - v; } v_endif;
      dst_reg[0] = as<vFloat> (v);
      dst_reg++;
    }
}
