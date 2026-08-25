// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-store-fold -mtt-tensix-optimize-invariant-loadi -fdump-tree-rvtt_store_fold -fdump-tree-rvtt_invariant" }
// Composition twin (FH audit, FOLDS P5; zero-twin pair in the census):
// store-fold S2 sink inside a counted loop with invariant-loadi on.
// HISTORY: this twin originally ALSO scanned a cc-restore-unbalanced
// refusal — an artifact of the old sfpi CC::pop rolled popc loop, whose
// CFG shape left an unbalanceable cc-restore candidate.  The laneGU
// sfpi fix (CC::pop constant-foldable popc chain, 2026-08-25) removes
// that shape from every sfpi-generated body (final bytes were identical
// either way — verified); the refusal path keeps its own direct
// raw-builtin witness in tensix/ccrestore-loadi-underflow-pop-bh.C.
// The composition fact that remains: the sink fires and the invariant
// immediate still hoists cleanly.
// { dg-final { scan-tree-dump "store-fold: sank post-region store" "rvtt_store_fold" } }
// { dg-final { scan-tree-dump "Hoisted invariant SFPU immediate" "rvtt_invariant" } }
extern volatile unsigned __instrn_buffer[];
namespace ckernel { constexpr inline volatile unsigned (&instrn_buffer)[] = ::__instrn_buffer; }
#include <sfpi.h>
using namespace sfpi;
__attribute__((noinline)) void
probe_sink_loop ()
{
  for (int ix = 0; ix < 8; ++ix)
    {
      vInt x = dst_reg[0];
      v_if (x >= 42) { x = x & 0x7ffff; } v_endif;
      dst_reg[0] = x;
      dst_reg++;
    }
}
