// P0/D1, loop path: the preheader trailing enable of a proven face-loop
// region must ALSO carry the all-lanes proof.  A lanes-off SFPENCC in
// the dominating position (the exact position the proven shape's
// pushc/popc enable occupies in macro-planner-typecast-face-loop-bh.C)
// is found as the trailing pure CC write but fails the word-exact
// proof: formation refuses by name and the bytes stay flags-off.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump-times "Macro-planner formation-refusal: cc-enable-unproved" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "Macro-planner formed" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-not "SFPLOADMACRO" } }
// { dg-final { scan-assembler-not "SFPCONFIG" } }
// { dg-final { scan-assembler-not "\\.ttinsn" } }
// { dg-final { scan-assembler-times "SFPENCC" 1 } }
// The explicit face loop survives: typed face transitions and row
// increments untouched (replay may compress the identical rows).
// { dg-final { scan-assembler-times "TTSETRWC" 2 } }
// { dg-final { scan-assembler-times "TTINCRWC" 8 } }
// { dg-final { scan-assembler "SFPSTOCHRND" } }

#include "macro-planner-typecast-faces-body.h"

__attribute__((noinline)) void lanes_off_face_loop (unsigned faces)
{
  __builtin_rvtt_sfpencc (0, 10);
  for (unsigned face = 0; face < faces; ++face)
    FACE ();
}
