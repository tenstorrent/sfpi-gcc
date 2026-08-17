// Cross-function shape (the typecast blocker's real-kernel form): the
// four architectural faces iterate in a loop inside a function that also
// carries opaque caller code -- a raw MMIO instruction push and an asm
// issue the typed vocabulary cannot see -- so the function-global
// ownership proof fails.  The region-scoped window forms anyway: the
// configuration prefix (all-lanes enable copy, owned SETC16 program,
// four descriptor words) sits once at the proven structural preheader's
// tail, each trip is eight alternating-VD launches, the drain, and the
// preserved typed TTSETRWC face transitions -- the one-configuration,
// dynamically-launched (32 per four-face tile) schedule.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump "Macro-planner config-ownership: loop-scoped window" "rvtt_macro_planner" } }
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
