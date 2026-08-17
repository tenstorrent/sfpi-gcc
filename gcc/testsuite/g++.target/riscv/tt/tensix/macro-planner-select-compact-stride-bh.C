// WP10 near-miss: a uniform-mode select whose typed Dst stride is 4.
// The tables' address-modifier machinery proves only the Dst += 2
// program, so the compact candidate refuses by name and the
// established 4-slot calendar forms with the separator kept -- its
// stride value is free (re-emitted verbatim), only the compact
// absorption is envelope-bound.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump "Macro-planner schedule-candidate: cc-compact" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner schedule-refusal: cc-template-unproved" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner descriptor-word dest=8: 0x00000706" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner descriptor-cc: sense=complement def-visible=2 pre-load=1 post-load=2 store-latch=0 restore-visible=4 interval=4 separator=kept" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner formed: rows=8 runs=1" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-times "TTINCRWC" 8 } }
// { dg-final { scan-assembler-not "SFPLOAD\\tL0, 64, 6, 6" } }

#define SELECT_ADDR_MODE 7
#define SELECT_COND_MODE 6
#define SELECT_STRIDE 4
#include "macro-planner-select-body.h"

__attribute__((noinline)) void select_stride_four ()
{
  __builtin_rvtt_sfppushc (0);
  __builtin_rvtt_sfppopc (0);
  SELECT_ROWS_8 ();
}
