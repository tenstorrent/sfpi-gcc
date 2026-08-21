// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-invariant-loadi -fdump-tree-rvtt_invariant-details" }
// THE wrong-code shape for a depth-0 CC write: after inlining, the
// loop sits under a caller v_if (narrowed preheader mask) while its
// body re-enables all lanes (SFPENCC) before the load.  The load's
// mask is WIDER than the preheader's: a naive hoist writes only the
// narrowed lanes, corrupting every lane the original wrote.  The
// depth-0 CC write refuses the loop by name.
// { dg-final { scan-tree-dump "cc-restore-ambient-cc-write" "rvtt_invariant" } }
// { dg-final { scan-tree-dump-not "Hoisted invariant SFPU immediate" "rvtt_invariant" } }
extern volatile unsigned __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>

static __attribute__((always_inline)) inline void
helper ()
{
  for (int d = 0; d < 32; ++d)
    {
      sfpi::vFloat v = sfpi::dst_reg[0];
      __builtin_rvtt_sfpencc (3, 10);
      sfpi::vFloat c = sfpi::vFloat (7.5f) * v;
      sfpi::dst_reg[0] = c;
      sfpi::dst_reg++;
    }
}

__attribute__((noinline)) void
ccrestore_callermask ()
{
  sfpi::vFloat g = sfpi::dst_reg[0];
  v_if (g == 0.0f)
    {
      helper ();
    }
  v_endif;
  sfpi::dst_reg[0] = g;
}
