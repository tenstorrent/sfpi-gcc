#include "rtl-lane-reset.h"

// Check that the typecast face loop forms on Wormhole with the single-slot Base=1 SETC16 program in the preheader.
// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump "Macro-planner formed: rows=8 runs=1 config=preheader" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-times "SFPENCC" 2 } }
// { dg-final { scan-assembler-times "SFPCONFIG" 5 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2466676736" 4 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2467725312" 4 } }
// { dg-final { scan-assembler-times "SFPNOP" 3 } }
// { dg-final { scan-assembler-times "TTSETRWC\\t0, 4, 8, 0, 0, 4" 2 } }
// { dg-final { scan-assembler-not "TTINCRWC" } }
// { dg-final { scan-assembler-not "SFPSTOCHRND" } }

#include "macro-planner-typecast-faces-body.h"

__attribute__((noinline)) void typecast_face_loop (unsigned faces)
{
  RVTT_TEST_LANE_RESET ();
  __builtin_rvtt_sfppushc (0);
  __builtin_rvtt_sfppopc (0);
  for (unsigned face = 0; face < faces; ++face)
    FACE ();
}
