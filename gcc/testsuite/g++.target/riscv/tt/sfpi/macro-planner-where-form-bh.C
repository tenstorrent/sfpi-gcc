#include "../tensix/rtl-lane-reset.h"

// Check that eight F16b/U16 predicated-select rows refuse macro scheduling because the CC restore would race the store (cc-restore-store-race).
// { dg-options "-mcpu=tt-bh-tensix -O3 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump "Macro-planner schedule-candidate: cc-compact" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner descriptor-refusal: cc-template-unproved" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner descriptor-refusal: cc-restore-store-race" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "Macro-planner schedule-refusal: cc-separator-kept-silicon-unproven" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "Macro-planner descriptor-cc:" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "Macro-planner formed" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-not "\\.ttinsn" } }
// { dg-final { scan-assembler-not {SFPCONFIG[ \t]+([0-9]|1[0-4]),} } }
// { dg-final { scan-assembler-not "SFPLOADMACRO" } }
// { dg-final { scan-assembler-times "SFPSETCC" 1 } }
// { dg-final { scan-assembler-times "SFPSTORE" 1 } }
// { dg-final { scan-assembler-times "TTINCRWC" 8 } }

namespace ckernel { volatile unsigned long *instrn_buffer; }
#include <sfpi.h>
using namespace sfpi;

#define WHERE_ROW()                                                           \
  do                                                                          \
    {                                                                         \
      __builtin_rvtt_sfppushc (0);                                            \
      __builtin_rvtt_sfppopc (0);                                             \
      vFloat condition = dst_reg[0].mode<DataLayout::F16b> ();                \
      vUInt on_true = dst_reg[16].mode<DataLayout::U16> ();                   \
      vUInt on_false = dst_reg[32].mode<DataLayout::U16> ();                  \
      vUInt result = on_false;                                                \
      v_if (condition != 0.0f)                                                \
	{                                                                     \
	  result = on_true;                                                   \
	}                                                                     \
      v_endif;                                                                \
      dst_reg[0].mode<DataLayout::U16> () = result;                           \
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);                                   \
    }                                                                         \
  while (0)

__attribute__((noinline)) void predicated_select_rows ()
{
  RVTT_TEST_LANE_RESET ();
  WHERE_ROW ();
  WHERE_ROW ();
  WHERE_ROW ();
  WHERE_ROW ();
  WHERE_ROW ();
  WHERE_ROW ();
  WHERE_ROW ();
  WHERE_ROW ();
}
