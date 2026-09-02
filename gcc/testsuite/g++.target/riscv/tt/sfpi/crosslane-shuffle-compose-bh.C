// Shuffle-of-shuffle composition (X4): the surface's
// constant-pattern shuffle<> canonicalizes rotation patterns to
// subvec_rotr chains at compile time, so composed shuffles inline into
// ONE ror1 chain, which the rotate-collapse rule reduces modulo 8 --
// here rot-by-2 o rot-by-6 == identity, and rot-by-5 o rot-by-1 ==
// rot-by-6.
// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-crosslane -mno-tt-tensix-optimize-replay -fdump-tree-rvtt_crosslane" }

namespace ckernel { unsigned *instrn_buffer; }
#include <sfpi.h>
using namespace sfpi;

void shuffled_round_trip ()
{
  __builtin_rvtt_sfpencc_all_lanes ();
  vFloat v = dst_reg[0];
  // rotation by 2: result[col] = v[(col - 2) & 7].
  vFloat a = shuffle<6, 7, 0, 1, 2, 3, 4, 5> (v);
  // rotation by 6 undoes it.
  vFloat b = shuffle<2, 3, 4, 5, 6, 7, 0, 1> (a);
  dst_reg[2] = b;
}

void shuffled_compose ()
{
  __builtin_rvtt_sfpencc_all_lanes ();
  vFloat w = dst_reg[4];
  vFloat c = shuffle<3, 4, 5, 6, 7, 0, 1, 2> (w);	// rot 5
  vFloat d = shuffle<7, 0, 1, 2, 3, 4, 5, 6> (c);	// rot 1
  dst_reg[6] = d;
}

// { dg-final { scan-tree-dump "rotate chain collapse 8 -> 0 links" "rvtt_crosslane" } }
// shuffled_compose: 5+1 rotations fuse into ONE 6-link chain (6 mod 8
// == 6: nothing to collapse, the chain stays literal).
// round trip: zero shuffles; compose: 6 remain.
// { dg-final { scan-assembler-times {SFPSHFT2\tL[0-9]+, L[0-9]+, 0, 3} 6 } }
