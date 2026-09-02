// Typecast four-face shape, unrolled form, Wormhole: the shared descriptor's owned
// SETC16 program is the SINGLE Base=1 slot (physical slot 6, regs
// 19/29/54) -- the launch's two-bit selector reaches it through the
// pinned ADDR_MOD_SET_Base=1 SFPU platform contract; the base-0 bank
// (slot 2, regs 11/25/50 = LLK's live ADDR_MOD_2) is never written
// (hardware-adjudicated).
// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner" }
// { dg-final { scan-assembler-times "SFPENCC" 1 } }
// { dg-final { scan-assembler-times "SFPCONFIG" 4 } }
// { dg-final { scan-assembler-not "\\.ttinsn\\t2987065344" } }
// { dg-final { scan-assembler-not "\\.ttinsn\\t2987982850" } }
// { dg-final { scan-assembler-not "\\.ttinsn\\t2989621248" } }
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
