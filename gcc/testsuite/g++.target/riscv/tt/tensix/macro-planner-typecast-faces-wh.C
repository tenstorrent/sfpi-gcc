// WP8 step 4, unrolled form, Wormhole: the shared descriptor's owned
// SETC16 program covers BOTH physical address-modifier banks (slots 2
// and 6) because the launch's two-bit selector maps through the
// unencoded incoming Base state -- the dual-slot bank-base ownership
// proof is the emitted program itself.
// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner" }
// { dg-final { scan-assembler-times "SFPENCC" 1 } }
// { dg-final { scan-assembler-times "SFPCONFIG" 4 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2987065344" 1 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2987982850" 1 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2989621248" 1 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2987589632" 1 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2988244994" 1 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2989883392" 1 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2466676736" 16 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2467725312" 16 } }
// { dg-final { scan-assembler-times "SFPNOP" 12 } }
// { dg-final { scan-assembler-times "TTSETRWC\\t0, 4, 8, 0, 0, 4" 8 } }
// { dg-final { scan-assembler-not "TTINCRWC" } }
// { dg-final { scan-assembler-not "SFPSTOCHRND" } }
// { dg-final { scan-assembler-not "SFPSTORE" } }

#include "macro-planner-typecast-faces-body.h"

__attribute__((noinline)) void typecast_four_faces ()
{
  __builtin_rvtt_sfppushc (0);
  __builtin_rvtt_sfppopc (0);
  FACE (); FACE (); FACE (); FACE ();
}
