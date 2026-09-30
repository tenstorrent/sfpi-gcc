// The canonical slide recognizer admits exactly a depth-1 predicate token.
// A depth-2 token is a valid structured condition, but it is not the proven
// slide frame and must remain untouched.
// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-crosslane -mno-tt-tensix-optimize-replay -fdump-tree-rvtt_crosslane" }

namespace ckernel { volatile unsigned long *instrn_buffer; }
#include <sfpi.h>
using namespace sfpi;

void depth_two_slide_near_miss ()
{
  __builtin_rvtt_sfpencc_all_lanes ();
  vFloat v = dst_reg[0];
  vFloat r = subvec_rotr<2> (v);
  {
    impl_::CC cc;
    cc.push ().push ().if_().cond (lane_col () < 2);
    r = 0.0f;
  }
  dst_reg[2] = r;
}

// { dg-final { scan-tree-dump-not "re-lowered" "rvtt_crosslane" } }
// { dg-final { scan-assembler-not {SFPSHFT2\tL[0-9]+, L[0-9]+, 0, 4} } }
