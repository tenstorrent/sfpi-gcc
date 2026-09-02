// X6 replay-formation barrier twin: the face-transpose family
// carries xtt_replay=barrier and opaque effects -- the replay-hoist pass
// must NOT capture any of the choreography into a TTREPLAY record (its
// Dst/config effects are unmodeled; FS persistence rules make an
// unproven capture a cross-launch hazard).  The loop is the classic
// capture bait.
// { dg-options "-mcpu=tt-bh-tensix -O2 -std=c++17 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-replay-hoist" }

namespace ckernel { unsigned *instrn_buffer; }
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
