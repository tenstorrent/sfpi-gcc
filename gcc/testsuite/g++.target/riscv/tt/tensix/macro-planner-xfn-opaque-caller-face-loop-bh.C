#include "rtl-lane-reset.h"

// Check that a face loop in a function with opaque raw issues forms through the region-scoped window.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump "Macro-planner config-ownership: loop-scoped window" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner formed: rows=8 runs=1 config=preheader" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-times "SFPENCC" 2 } }
// { dg-final { scan-assembler-times "SFPCONFIG" 5 } }
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
  RVTT_TEST_LANE_RESET ();
  instruction_pipe = 0x02000000;	/* NOP, whether stored or delivered.  */
  asm volatile ("fence rw,rw" ::: "memory");	/* Scalar opaque channel.  */
  asm volatile (".ttinsn\t%0" :: "i" (4)); /* Audited opcode-0 NOP.  */
  for (unsigned face = 0; face < faces; ++face)
    {
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfppopc (0);
      FACE ();
    }
}
