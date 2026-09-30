// A slide's rotate tail must be consumed only by the predicated-zero merge.
// Rewriting the tail's ROR1 links to SHR1 while an external tap survives would
// change that tap from a rotation into a zero-fill shift.
// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-crosslane -mno-tt-tensix-optimize-replay -fdump-tree-rvtt_crosslane" }

namespace ckernel { volatile unsigned long *instrn_buffer; }
#include <sfpi.h>
using namespace sfpi;

void slide_two_with_tap ()
{
  __builtin_rvtt_sfpencc_all_lanes ();
  vFloat v = dst_reg[0];
  vFloat r = subvec_rotr<2> (v);
  vFloat tap = r;
  v_if (lane_col () < 2) {
    r = 0.0f;
  } v_endif;
  dst_reg[2] = r;
  dst_reg[4] = tap;
}

// { dg-final { scan-tree-dump-not "re-lowered" "rvtt_crosslane" } }
// { dg-final { scan-assembler-times {SFPSHFT2\tL[0-9]+, L[0-9]+, 0, 3} 2 } }
// { dg-final { scan-assembler-not {SFPSHFT2\tL[0-9]+, L[0-9]+, 0, 4} } }
