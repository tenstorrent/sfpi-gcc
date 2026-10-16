// Check that slides are not re-lowered on Wormhole, where SUBVEC_SHFLSHR1 lane 0 is unpredictable (SFPSHFT2.md).
// { dg-options "-mcpu=tt-wh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-crosslane -mno-tt-tensix-optimize-replay -fdump-tree-rvtt_crosslane" }

namespace ckernel { volatile unsigned long *instrn_buffer; }
#include <sfpi.h>
using namespace sfpi;

void slide_two ()
{
  __builtin_rvtt_sfpencc_all_lanes ();
  vFloat v = dst_reg[0];
  vFloat r = subvec_rotr<2> (v);
  v_if (lane_col () < 2) {
    r = 0.0f;
  } v_endif;
  dst_reg[2] = r;
}

// { dg-final { scan-tree-dump "crosslane-shflshr1-unsupported" "rvtt_crosslane" } }
// { dg-final { scan-tree-dump-not "re-lowered" "rvtt_crosslane" } }
// { dg-final { scan-assembler-times {SFPSHFT2\tL[0-9]+, L[0-9]+, 0, 3} 2 } }
// { dg-final { scan-assembler-not {SFPSHFT2\tL[0-9]+, L[0-9]+, 0, 4} } }
