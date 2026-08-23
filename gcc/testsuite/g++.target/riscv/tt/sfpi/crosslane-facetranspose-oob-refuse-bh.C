// X6 named refusal: face row beyond Dst (lane FV).
// { dg-options "-mcpu=tt-bh-tensix -O2 -std=c++17 -I [SFPI]/include -fno-exceptions -fno-rtti" }

namespace ckernel { unsigned *instrn_buffer; }
#include <sfpi.h>

void out_of_dst ()
{
  sfpi::face_transpose_dst_32b<1024> ();
}

// { dg-error "crosslane-facetranspose-row-unaligned" "" { target *-*-* } 0 }
