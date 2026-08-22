// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-store-fold -mtt-tensix-optimize-invariant-loadi -fdump-tree-rvtt_store_fold -fdump-tree-rvtt_invariant" }
// Composition twin (FH audit, FOLDS P5; zero-twin pair in the census):
// store-fold S2 sink inside a counted loop with invariant-loadi on.  The
// sink deletes the post-region store; the invariant pass sees the shorter
// body and the sunk store forfeits the cc-restore hoist candidate BY NAME
// (fail-closed pricing interaction, filed to the EK/EL owners).
// { dg-final { scan-tree-dump "store-fold: sank post-region store" "rvtt_store_fold" } }
// { dg-final { scan-tree-dump "cc-restore-unbalanced" "rvtt_invariant" } }
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
