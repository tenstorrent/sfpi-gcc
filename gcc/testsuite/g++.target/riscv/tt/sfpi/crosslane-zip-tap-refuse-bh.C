// Check that a zip chain with an external consumer of an inner frame is not collapsed (crosslane-frame-value-escape).
// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-crosslane -mno-tt-tensix-optimize-replay -fdump-tree-rvtt_crosslane" }

namespace ckernel { volatile unsigned long *instrn_buffer; }
#include <sfpi.h>
using namespace sfpi;

void zip3_tapped ()
{
  __builtin_rvtt_sfpencc_all_lanes ();
  __builtin_rvtt_sfpconfig_i (0, 15, 1);
  vFloat a = dst_reg[0], b = dst_reg[2];
  rowvec_zip (a, b);        // z1
  dst_reg[8] = a;           // external tap of z1's out_a
  rowvec_zip (a, b);        // z2
  rowvec_zip (a, b);        // z3
  dst_reg[4] = a;
  dst_reg[6] = b;
}

// { dg-final { scan-tree-dump "crosslane-frame-value-escape" "rvtt_crosslane" } }
// { dg-final { scan-tree-dump-not "zip chain collapse" "rvtt_crosslane" } }
// All three frames survive: 3 transposes, 6 mod-0 swaps.
// { dg-final { scan-assembler-times {SFPTRANSP} 3 } }
// { dg-final { scan-assembler-times {SFPSWAP\tL[0-9]+, L[0-9]+, 0} 6 } }
