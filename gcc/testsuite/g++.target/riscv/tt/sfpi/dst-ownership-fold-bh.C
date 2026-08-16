// Track B (18.9 B4): a proven-identity Dst reload -- same typed address,
// mode, and address-mode tuple, no RWC/layout boundary, no store, no
// opacity between, first load under provably all-lanes CC -- folds into
// the LREG-resident value.  Mirror of the erfinv author-reload shape.
// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-dst-ownership -fdump-rtl-rvtt_dst_ownership-details" }
// { dg-final { scan-rtl-dump-times "Dst-ownership fold: reload insn" 1 "rvtt_dst_ownership" } }
// { dg-final { scan-rtl-dump-times "1 reload.s. folded" 1 "rvtt_dst_ownership" } }
// { dg-final { scan-assembler-times {\mSFPLOAD\t} 1 } }
// { dg-final { scan-assembler-times {\mSFPSTORE\t} 1 } }

#include "dst-ownership-prologue.h"

using namespace sfpi;

__attribute__((noinline)) void
reload_for_register_pressure ()
{
  vFloat in = dst_reg[0];
  vFloat r = in * 0.30102999f + 1.25f;
  v_if (in == 0.0F) { r = -2.5f; }
  v_endif;
  r = r * in + 0.125f;
  vFloat in2 = dst_reg[0];		// author reload: provable identity
  dst_reg[0] = copysgn (r, in2);
}
