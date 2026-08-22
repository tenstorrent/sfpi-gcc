// Wormhole capability refusal (crosslane-shflshr1-unsupported):
// WormholeB0 SFPSHFT2.md gives SUBVEC_SHFLSHR1's lane 0 an
// UnpredictableValue, so the capability table has no zero-fill shift
// and the canonical rotate+mask form -- WH's own correct lowering --
// is kept byte-identically.
// { dg-options "-mcpu=tt-wh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-crosslane -mno-tt-tensix-optimize-replay -fdump-tree-rvtt_crosslane" }

namespace ckernel { unsigned *instrn_buffer; }
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
