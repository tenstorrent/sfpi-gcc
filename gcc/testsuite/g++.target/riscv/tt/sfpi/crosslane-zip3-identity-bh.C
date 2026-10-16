// Check that a chain of three rowvec_zip frames on one pair collapses to nothing.
// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-crosslane -mno-tt-tensix-optimize-replay -fdump-tree-rvtt_crosslane" }

namespace ckernel { volatile unsigned long *instrn_buffer; }
#include <sfpi.h>
using namespace sfpi;

void zip_round_trip ()
{
  __builtin_rvtt_sfpencc_all_lanes ();
  vFloat a = dst_reg[0], b = dst_reg[2];
  rowvec_zip (a, b);
  rowvec_zip (a, b);
  rowvec_zip (a, b);
  dst_reg[4] = a;
  dst_reg[6] = b;
}

// Two zips == unzip, the canonical lowering; left alone.
void unzip_stays ()
{
  __builtin_rvtt_sfpencc_all_lanes ();
  vFloat x = dst_reg[8], y = dst_reg[10];
  rowvec_unzip (x, y);
  dst_reg[12] = x;
  dst_reg[14] = y;
}

// { dg-final { scan-tree-dump "zip chain collapse 3 -> 0 frames" "rvtt_crosslane" } }
// zip_round_trip contributes nothing; unzip_stays keeps 2 transposes
// and 4 mod-0 swaps.
// { dg-final { scan-assembler-times {SFPTRANSP} 2 } }
// { dg-final { scan-assembler-times {SFPSWAP\tL[0-9]+, L[0-9]+, 0} 4 } }
