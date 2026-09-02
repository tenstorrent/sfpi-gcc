// X6 named refusal: unaligned face row.  The static_assert
// fires in the header at instantiation; match it file-wide (line 0).
// { dg-options "-mcpu=tt-bh-tensix -O2 -std=c++17 -I [SFPI]/include -fno-exceptions -fno-rtti" }

namespace ckernel { unsigned *instrn_buffer; }
#include <sfpi.h>

void unaligned ()
{
  sfpi::face_transpose_dst_32b<8> ();
}

// { dg-error "crosslane-facetranspose-row-unaligned" "" { target *-*-* } 0 }
