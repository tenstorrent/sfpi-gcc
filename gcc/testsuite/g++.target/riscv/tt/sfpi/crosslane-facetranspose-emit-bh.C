// X6 surface emission (lane FV): a two-face 32-bit Dst face transpose
// under one cfg block must emit exactly the hand choreography counts
// (topk_xl transpose_dest_face_32b x2): per face 8 TTMOVD2B + 2
// TTTRNSPSRCB + 4 TTMOVB2A + 4 TTMOVB2D + 2 TTMOVA2D + 5 config-byte
// words; block-wide 2 TTSETC16 + 2 zero-flag RMWs + 1 TTSTALLWAIT; one
// TTSETRWC bank release.  Also the genericity check: face row offsets
// appear literally.
// { dg-options "-mcpu=tt-bh-tensix -O2 -std=c++17 -I [SFPI]/include -fno-exceptions -fno-rtti" }

namespace ckernel { unsigned *instrn_buffer; }
#include <sfpi.h>
using namespace sfpi;

void two_faces ()
{
  face_transpose_cfg_enter ();
  face_transpose_dst_32b<0> ();
  face_transpose_dst_32b<48> ();
  face_transpose_cfg_leave ();
  face_transpose_release_banks ();
}

// { dg-final { scan-assembler-times {TTMOVD2B\t} 16 } }
// { dg-final { scan-assembler-times {TTTRNSPSRCB} 4 } }
// { dg-final { scan-assembler-times {TTMOVB2A\t} 8 } }
// { dg-final { scan-assembler-times {TTMOVB2D\t} 8 } }
// { dg-final { scan-assembler-times {TTMOVA2D\t} 4 } }
// { dg-final { scan-assembler-times {TTSETC16\t2, 1} 1 } }
// { dg-final { scan-assembler-times {TTSETC16\t2, 0} 1 } }
// { dg-final { scan-assembler-times {TTSTALLWAIT\t128, 2432} 1 } }
// { dg-final { scan-assembler-times {TTSETRWC\t3, 0, 0, 0, 0, 7} 1 } }
// { dg-final { scan-assembler {TTMOVD2B\t1, 16, 7, 2, 48} } }
// { dg-final { scan-assembler {TTMOVA2D\t1, 8, 7, 2, 56} } }
