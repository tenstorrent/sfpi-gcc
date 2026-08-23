// X6 named refusal: only the Blackhole choreography is proven (WH lacks
// the implied-format audit and a hand vehicle) -- the surface refuses by
// name on Wormhole even though the builtins exist there (lane FV).
// { dg-options "-mcpu=tt-wh-tensix -O2 -std=c++17 -I [SFPI]/include -fno-exceptions -fno-rtti" }

namespace ckernel { unsigned *instrn_buffer; }
#include <sfpi.h>

void wh_face ()
{
  sfpi::face_transpose_dst_32b<0> ();
}

// { dg-error "crosslane-facetranspose-unsupported-target" "" { target *-*-* } 0 }
