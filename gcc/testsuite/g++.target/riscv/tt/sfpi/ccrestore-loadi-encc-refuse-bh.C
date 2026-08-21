// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-invariant-loadi -fdump-tree-rvtt_invariant-details" }
// Wrong-code-class admission edge: SFPENCC inside the region can WIDEN
// the enable set (SFPENCC.md), so the in-region load may write MORE
// lanes than the preheader would -- a naive hoist would leave those
// lanes corrupt.  The in-region candidate refuses by name; the restore
// proof itself still holds (the POPC discards the region state), so
// the depth-0 candidate still hoists.
// { dg-final { scan-tree-dump-times "cc-position-widening-unproven" 1 "rvtt_invariant" } }
// { dg-final { scan-tree-dump-times "Hoisted invariant SFPU immediate" 1 "rvtt_invariant" } }
extern volatile unsigned __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>

__attribute__((noinline)) void
ccrestore_encc_inregion ()
{
  for (int ix = 0; ix < 32; ++ix)
    {
      sfpi::vFloat v = sfpi::dst_reg[0];
      sfpi::vFloat r = v * 0.8125f;
      v_if (v == 0.0f)
	{
	  __builtin_rvtt_sfpencc (3, 10);
	  r = v * 5.5f;
	}
      v_endif;
      sfpi::dst_reg[0] = r;
      sfpi::dst_reg++;
    }
}
