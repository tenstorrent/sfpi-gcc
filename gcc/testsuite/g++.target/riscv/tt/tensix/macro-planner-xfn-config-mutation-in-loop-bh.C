// Near-miss refusal: a typed write to a planner-owned configuration
// destination INSIDE the face loop (between faces) is a foreign owner
// inside the scoped window; the region-scoped fallback must refuse with
// the established name and keep the bytes explicit.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump "config-ownership-unproven" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "loop-scoped window" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-not "SFPLOADMACRO" } }
// { dg-final { scan-assembler "SFPSTOCHRND" } }
// { dg-final { scan-assembler "TTINCRWC" } }
// { dg-final { scan-assembler "TTSETRWC\\t0, 4, 8, 0, 0, 4" } }

#include "macro-planner-typecast-faces-body.h"

volatile unsigned instruction_pipe;

__attribute__((noinline)) void face_loop_mutated (unsigned faces)
{
  instruction_pipe = 0x12300000;	/* raw MMIO instruction push */
  asm volatile (".ttinsn\t%0" :: "i" (4));	/* opaque raw issue */
  for (unsigned face = 0; face < faces; ++face)
    {
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfppopc (0);
      FACE ();
      auto knob = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0,
					  no_increment);
      __builtin_rvtt_sfpwriteconfig_v (knob, 4);
    }
}
