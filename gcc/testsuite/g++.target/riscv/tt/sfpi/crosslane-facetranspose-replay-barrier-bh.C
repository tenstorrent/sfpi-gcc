// Check that replay hoisting never captures face-transpose instructions in a loop.
// { dg-options "-mcpu=tt-bh-tensix -O2 -std=c++17 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-replay-hoist" }

namespace ckernel { volatile unsigned long *instrn_buffer; }
#include <sfpi.h>
using namespace sfpi;

void face_loop ()
{
  face_transpose_cfg_enter ();
  #pragma GCC unroll 1
  for (int i = 0; i < 4; ++i)
    face_transpose_dst_32b<32> ();
  face_transpose_cfg_leave ();
  face_transpose_release_banks ();
}

// { dg-final { scan-assembler-not {TTREPLAY} } }
// { dg-final { scan-assembler {TTTRNSPSRCB} } }
