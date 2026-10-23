// Check that rotate+predicated-zero slides re-lower to SUBVEC_SHFLSHR1 chains on Blackhole.
// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-crosslane -mno-tt-tensix-optimize-replay -fdump-tree-rvtt_crosslane" }

namespace ckernel { volatile unsigned long *instrn_buffer; }
#include <sfpi.h>
using namespace sfpi;

void slide_two ()
{
  __builtin_rvtt_sfpencc_all_lanes ();
  __builtin_rvtt_sfpconfig_i (0, 15, 1);
  vFloat v = dst_reg[0];
  vFloat r = subvec_rotr<2> (v);
  v_if (lane_col () < 2) {
    r = 0.0f;
  } v_endif;
  dst_reg[2] = r;
}

void renamed_slide_three ()
{
  __builtin_rvtt_sfpencc_all_lanes ();
  __builtin_rvtt_sfpconfig_i (0, 15, 1);
  vFloat w = dst_reg[4];
  vFloat s = subvec_rotr<3> (w);
  v_if (lane_col () < 3) {
    s = 0.0f;
  } v_endif;
  dst_reg[6] = s;
}

// Distance/predicate mismatch (rotate by 2, zero cols < 3) is not a
// slide; the region stays.
void mismatch_stays ()
{
  __builtin_rvtt_sfpencc_all_lanes ();
  __builtin_rvtt_sfpconfig_i (0, 15, 1);
  vFloat u = dst_reg[8];
  vFloat t = subvec_rotr<2> (u);
  v_if (lane_col () < 3) {
    t = 0.0f;
  } v_endif;
  dst_reg[10] = t;
}

// { dg-final { scan-tree-dump-times "slide<2> re-lowered to SUBVEC_SHFLSHR1" 1 "rvtt_crosslane" } }
// { dg-final { scan-tree-dump-times "slide<3> re-lowered to SUBVEC_SHFLSHR1" 1 "rvtt_crosslane" } }
// 2 + 3 zero-fill shifts; the mismatch keeps its 2 rotates.
// { dg-final { scan-assembler-times {SFPSHFT2\tL[0-9]+, L[0-9]+, 0, 4} 5 } }
// { dg-final { scan-assembler-times {SFPSHFT2\tL[0-9]+, L[0-9]+, 0, 3} 2 } }
