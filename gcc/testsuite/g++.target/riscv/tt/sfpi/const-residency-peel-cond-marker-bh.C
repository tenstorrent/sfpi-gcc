// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-lut-select -mtt-tensix-optimize-lut-select-fp16 -mtt-tensix-optimize-const-residency -fdump-tree-rvtt_prgm_const-details" }
// Residency fire, CC-canonical peel class, with structured condition
// markers left in the body.  The loop forms the six-entry LUT (so the
// magnitude tree's own CC region is gone) and then guards the store with
// a v_if on an integer encoding compare, the NaN pass-through shape of
// fresh_cpp/sigmoid_lut_licensed.h.  That v_if keeps its sfpxpred /
// sfpxcond markers through pass_rvtt_vif; they emit no Tensix word and
// are CC machinery to every other consumer, so the single-block body
// ending in the all-lanes SFPENCC is CC-canonical.  The proof used to
// refuse it as a volatile non-Dst effect (sfpxpred) and every LUT
// coefficient stayed in the loop.  The programming point must still
// follow the peeled iteration's all-lanes SFPENCC, so no hoisted
// constant is written with lanes disabled.
// { dg-final { scan-tree-dump-not "cc-canonical proof failed" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "admits the CC-canonical peel" 1 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "programming point follows the peeled all-lanes SFPENCC" 1 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "allocated PRGM L1\\d for constant 0x\[0-9a-f\]+ .loop class" 3 "rvtt_prgm_const" } }
// { dg-final { scan-assembler-times "SFPLUTFP32" 2 } }
// { dg-final { scan-assembler-times "SFPCONFIG" 3 } }

extern volatile unsigned long __instrn_buffer[];

namespace ckernel {
constexpr inline volatile unsigned long (&instrn_buffer)[] = ::__instrn_buffer;
}

#include <sfpi.h>

__attribute__((noinline)) void
lut_nan_guard ()
{
  for (int ix = 0; ix < 32; ++ix)
    {
      const sfpi::vFloat x = sfpi::dst_reg[0];
      const sfpi::vFloat a = sfpi::abs (x);
      sfpi::vFloat s = a * 0x1.214p-8f + 0x1.df4p-2f;
      v_if (a < 0.5f)
	{
	  s = a * 0x1.f88p-3f + 0x1p-15f;
	}
      v_elseif (a < 1.0f)
	{
	  s = a * 0x1.bd4p-3f + 0x1.e94p-7f;
	}
      v_elseif (a < 1.5f)
	{
	  s = a * 0x1.634p-3f + 0x1.e38p-5f;
	}
      v_elseif (a < 2.0f)
	{
	  s = a * 0x1.03cp-3f + 0x1.078p-3f;
	}
      v_elseif (a < 4.0f)
	{
	  s = a * 0x1.a08p-5f + 0x1.28cp-2f;
	}
      v_endif;
      s = sfpi::min (s, 0.5f);
      sfpi::vFloat r = sfpi::copysgn (s, x) + 0.5f;
      const sfpi::vFloat inf = __builtin_inff ();
      v_if (sfpi::as<sfpi::vInt> (sfpi::setsgn (x, 0))
	    > sfpi::as<sfpi::vInt> (inf))
	{
	  r = x;
	}
      v_endif;
      sfpi::dst_reg[0] = r;
      sfpi::dst_reg++;
    }
}
