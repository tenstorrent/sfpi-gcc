// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-store-fold -fdump-tree-rvtt_store_fold" }
// S2 on a float-format pair refuses by name: the float store conversion
// canonicalizes Dst (denormal flush), so the enabled-complement lanes'
// write-back is NOT a no-op -- tt/proofs/store-sink-roundtrip/ BF16 row
// (254/2^16 witnesses), FP16 and FP32 rows likewise.  The predicated
// mov stays; the semantic body's extra word versus a store-under-
// predicate hand kernel is a real Dst-canonicalization semantic
// difference, not recoverable delivery cost.
// { dg-final { scan-tree-dump "store-fold refused .store-fold-sink-format-canonicalizing" "rvtt_store_fold" } }
// { dg-final { scan-tree-dump "store-fold: forwarded=0 sunk=0" "rvtt_store_fold" } }
// { dg-final { scan-assembler "SFPMOV" } }
extern volatile unsigned __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>
__attribute__((noinline)) void
storefold_float_shrink (float lam)
{
  for (int ix = 0; ix < 8; ++ix)
    {
      const sfpi::vFloat v = sfpi::dst_reg[0];
      sfpi::vFloat r = v;
      v_if (sfpi::abs(v) <= lam)
	{
	  r = 0.0f;
	}
      v_endif;
      sfpi::dst_reg[0] = r;
      sfpi::dst_reg++;
    }
}
