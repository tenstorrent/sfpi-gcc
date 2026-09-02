// Typecast four-face shape, unrolled form: four typed-TTSETRWC-separated face runs
// share ONE region and ONE descriptor configuration (SFPENCC + owned
// SETC16 + four config words emitted once), with eight alternating-VD
// launches and a three-slot drain per face and every architectural face
// transition preserved in place.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner" }
// { dg-final { scan-assembler-times "SFPENCC" 1 } }
// { dg-final { scan-assembler-times "SFPCONFIG" 4 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2987524096" 1 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2988572674" 1 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2989817856" 1 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2466693120" 16 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2467741696" 16 } }
// { dg-final { scan-assembler-times "SFPNOP" 12 } }
// { dg-final { scan-assembler-times "TTSETRWC\\t0, 4, 8, 0, 0, 4" 8 } }
// { dg-final { scan-assembler-not "TTINCRWC" } }
// { dg-final { scan-assembler-not "SFPCAST" } }
// { dg-final { scan-assembler-not "SFPSTOCHRND" } }
// { dg-final { scan-assembler-not "SFPSTORE" } }

#include "macro-planner-typecast-faces-body.h"

__attribute__((noinline)) void typecast_four_faces ()
{
  __builtin_rvtt_sfppushc (0);
  __builtin_rvtt_sfppopc (0);
  FACE (); FACE (); FACE (); FACE ();
}
