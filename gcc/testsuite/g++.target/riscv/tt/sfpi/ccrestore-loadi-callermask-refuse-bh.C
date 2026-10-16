// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-invariant-loadi -fdump-tree-rvtt_invariant-details" }
// Check that a loop whose body re-enables all lanes under a caller v_if is not hoisted (cc-restore-ambient-cc-write).
// { dg-final { scan-tree-dump "cc-restore-ambient-cc-write" "rvtt_invariant" } }
// { dg-final { scan-tree-dump-not "Hoisted invariant SFPU immediate" "rvtt_invariant" } }
extern volatile unsigned long __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned long (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>

static __attribute__((always_inline)) inline void
helper ()
{
  for (int d = 0; d < 32; ++d)
    {
      sfpi::vFloat v = sfpi::dst_reg[0];
      __builtin_rvtt_sfpencc (10, 3);
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
