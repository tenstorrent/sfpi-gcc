// Check that face_transpose_dst_32b_batch<8> emits eight face transposes under one config block.
// { dg-options "-mcpu=tt-bh-tensix -O2 -std=c++17 -I [SFPI]/include -fno-exceptions -fno-rtti" }

namespace ckernel { volatile unsigned long *instrn_buffer; }
#include <sfpi.h>
using namespace sfpi;

void eight_faces ()
{
  face_transpose_dst_32b_batch<8> ();
  face_transpose_release_banks ();
}

// { dg-final { scan-assembler-times {TTTRNSPSRCB} 16 } }
// { dg-final { scan-assembler-times {TTMOVD2B\t} 64 } }
// { dg-final { scan-assembler-times {TTSETC16\t2, 1} 1 } }
// { dg-final { scan-assembler-times {TTSETC16\t2, 0} 1 } }
// { dg-final { scan-assembler-times {TTSTALLWAIT\t128, 2432} 1 } }
// { dg-final { scan-assembler {TTMOVB2D\t0, 28, 7, 4, 124} } }
