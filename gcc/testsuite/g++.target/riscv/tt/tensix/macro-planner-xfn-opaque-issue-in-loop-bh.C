// Near-miss refusal: a raw MMIO instruction push (volatile scalar
// store, the shape of every raw Tensix issue and configuration access
// the typed vocabulary cannot see) INSIDE the face loop sits between
// the materialization point and later launches; the region-scoped
// window must refuse and keep the bytes explicit.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump "config-ownership-unproven" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "loop-scoped window" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-not "SFPLOADMACRO" } }
// { dg-final { scan-assembler "SFPSTOCHRND" } }
// { dg-final { scan-assembler "TTINCRWC" } }

#include "macro-planner-typecast-faces-body.h"

volatile unsigned instruction_pipe;

__attribute__((noinline)) void face_loop_raw_issue (unsigned faces)
{
  instruction_pipe = 0x12300000;	/* raw MMIO instruction push */
  asm volatile (".ttinsn\t%0" :: "i" (4));	/* opaque raw issue */
  for (unsigned face = 0; face < faces; ++face)
    {
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfppopc (0);
      FACE ();
      instruction_pipe = 0x00990000;	/* raw issue between faces */
    }
}
