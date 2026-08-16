// WP8 step 4, dynamic form: the face loop's body region forms with the
// configuration hoisted once into the guarded structural preheader and
// the all-lanes proof taken from the dominating enable outside the loop
// (no Tensix issue sits between it and the loop entry).  Each trip is
// eight alternating launches, the drain, and the preserved TTSETRWC
// face transitions -- the one-configuration, dynamic-launch schedule.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump "Macro-planner formed: rows=8 runs=1 config=preheader" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-times "SFPENCC" 1 } }
// { dg-final { scan-assembler-times "SFPCONFIG" 4 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2466693120" 4 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2467741696" 4 } }
// { dg-final { scan-assembler-times "SFPNOP" 3 } }
// { dg-final { scan-assembler-times "TTSETRWC\\t0, 4, 8, 0, 0, 4" 2 } }
// { dg-final { scan-assembler-not "TTINCRWC" } }
// { dg-final { scan-assembler-not "SFPSTOCHRND" } }

#include "macro-planner-typecast-faces-body.h"

__attribute__((noinline)) void typecast_face_loop (unsigned faces)
{
  __builtin_rvtt_sfppushc (0);
  __builtin_rvtt_sfppopc (0);
  for (unsigned face = 0; face < faces; ++face)
    FACE ();
}
