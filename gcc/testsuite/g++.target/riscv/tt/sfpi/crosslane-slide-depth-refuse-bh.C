// An enclosing row predicate changes the effective zero-fill condition.
// The slide recognizer must not relower this nested, valid v_if graph as a
// standalone column-predicated slide.
// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-crosslane -mno-tt-tensix-optimize-replay -fdump-tree-rvtt_crosslane" }

namespace ckernel { volatile unsigned long *instrn_buffer; }
#include <sfpi.h>
using namespace sfpi;

void depth_two_slide_near_miss ()
{
  __builtin_rvtt_sfpencc_all_lanes ();
  vFloat v = dst_reg[0];
  vFloat r = subvec_rotr<2> (v);
  v_if (lane_row () < 2) {
    v_if (lane_col () < 2) { r = 0.0f; }
    v_endif;
  }
  v_endif;
  dst_reg[2] = r;
}

// { dg-final { scan-tree-dump-not "re-lowered" "rvtt_crosslane" } }
// { dg-final { scan-assembler-not {SFPSHFT2\tL[0-9]+, L[0-9]+, 0, 4} } }
