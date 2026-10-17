// Check that face_transpose is rejected on Wormhole (crosslane-facetranspose-unsupported-target).
// { dg-options "-mcpu=tt-wh-tensix -O2 -std=c++17 -I [SFPI]/include -fno-exceptions -fno-rtti" }

namespace ckernel { volatile unsigned long *instrn_buffer; }
#include <sfpi.h>

void wh_face ()
{
  sfpi::face_transpose_dst_32b<0> ();
}

// { dg-error "crosslane-facetranspose-unsupported-target" "" { target *-*-* } 0 }
