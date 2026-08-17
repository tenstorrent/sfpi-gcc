// Cross-function shape, Wormhole: the opaque-caller face loop forms
// through the region-scoped window with the single-slot Base=1 owned
// SETC16 program (physical slot 6 under the pinned ADDR_MOD_SET_Base=1
// SFPU platform contract, sfpi-gcc 2a0ba1e6602) in the preheader.
// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump "Macro-planner config-ownership: loop-scoped window" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner formed: rows=8 runs=1 config=preheader" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-times "SFPENCC" 1 } }
// { dg-final { scan-assembler-times "SFPCONFIG" 4 } }
// { dg-final { scan-rtl-dump-times "Macro-planner descriptor-setc16" 3 "rvtt_macro_planner" } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2466676736" 4 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2467725312" 4 } }
// { dg-final { scan-assembler-times "SFPNOP" 3 } }
// { dg-final { scan-assembler-times "TTSETRWC\\t0, 4, 8, 0, 0, 4" 2 } }
// { dg-final { scan-assembler-not "TTINCRWC" } }
// { dg-final { scan-assembler-not "SFPSTOCHRND" } }

#include "macro-planner-typecast-faces-body.h"

volatile unsigned instruction_pipe;

__attribute__((noinline)) void opaque_caller_face_loop (unsigned faces)
{
  instruction_pipe = 0x12300000;	/* raw MMIO instruction push */
  asm volatile (".ttinsn\t%0" :: "i" (4));	/* opaque raw issue */
  for (unsigned face = 0; face < faces; ++face)
    {
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfppopc (0);
      FACE ();
    }
}
