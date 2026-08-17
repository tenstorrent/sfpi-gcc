// Near-miss refusal: a second, DIFFERENT descriptor's row between the
// faces (a shift/cast row after each cast/round face) is not isomorphic
// to the region's rows, so it stays outside the region; its Tensix
// issues inside the loop body are foreign to the scoped window, so the
// region-scoped fallback must refuse and keep the bytes explicit -- no
// unsound sharing of one configuration across two different
// descriptors.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump "config-ownership-unproven" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "loop-scoped window" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-not "SFPLOADMACRO" } }
// { dg-final { scan-assembler "SFPSTOCHRND" } }
// { dg-final { scan-assembler "SFPSHFT" } }
// { dg-final { scan-assembler "TTINCRWC" } }

#include "macro-planner-typecast-faces-body.h"

volatile unsigned instruction_pipe;

__attribute__((noinline)) void face_loop_two_descriptors (unsigned faces)
{
  instruction_pipe = 0x12300000;	/* raw MMIO instruction push */
  asm volatile (".ttinsn\t%0" :: "i" (4));	/* opaque raw issue */
  for (unsigned face = 0; face < faces; ++face)
    {
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfppopc (0);
      FACE ();
      /* A different row shape (different descriptor) between faces.  */
      auto other = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0,
					   no_increment);
      auto shifted = __builtin_rvtt_sfpshft_i (nullptr, other, -31, 0, 0, 0);
      auto recast = __builtin_rvtt_sfpcast (shifted, 0);
      __builtin_rvtt_sfpstore (nullptr, recast, 0, 0, 0, 0, no_increment);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
