// Check that face_transpose on an unaligned face row is rejected by the header's static_assert.
// { dg-options "-mcpu=tt-bh-tensix -O2 -std=c++17 -I [SFPI]/include -fno-exceptions -fno-rtti" }

namespace ckernel { volatile unsigned long *instrn_buffer; }
#include <sfpi.h>

void unaligned ()
{
  sfpi::face_transpose_dst_32b<8> ();
}

// { dg-error "crosslane-facetranspose-row-unaligned" "" { target *-*-* } 0 }
