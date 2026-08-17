// The canonical SFPI where selector (the TTNN Where kernel's v_if
// spelling, F16b condition / U16 payloads -- the fp16b class), eight
// rows, REFUSING by the architectural name since the 2026-08-17 Where
// silicon adjudication (tt-quietbox-0, BH p150; evidence root
// ~/sfpi-uplift/where-adjudication-20260817, verdicts/VERDICT.md):
// this is the exact source class whose formed separator-kept 4-slot
// calendar (misc 0x706) mis-selected on silicon deterministically
// across two resets while the byte-identical binaries passed CRAQ in
// the generic sim -- root-caused by craq-sim 9f324140 to the live
// store lane mask: the calendar retires its all-lanes restore in the
// Delay-2 store's own cycle, so the store executes under the SFPSETCC
// complement mask.  The mixed-mode compact candidate refuses its
// descriptor by name, the established calendar's descriptor derives
// restore exec == store exec == 3 and refuses cc-restore-store-race,
// and the eight rows stay byte-identically on the semantic
// (planner-OFF) lowering -- the silicon-green form.
// { dg-options "-mcpu=tt-bh-tensix -O3 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump "Macro-planner schedule-candidate: cc-compact" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner descriptor-refusal: cc-template-unproved" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner descriptor-refusal: cc-restore-store-race" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "Macro-planner schedule-refusal: cc-separator-kept-silicon-unproven" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "Macro-planner descriptor-cc:" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "Macro-planner formed" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-not "\\.ttinsn" } }
// { dg-final { scan-assembler-not "SFPCONFIG" } }
// { dg-final { scan-assembler-not "SFPLOADMACRO" } }
// { dg-final { scan-assembler-times "SFPSETCC" 1 } }
// { dg-final { scan-assembler-times "SFPSTORE" 1 } }
// { dg-final { scan-assembler-times "TTINCRWC" 8 } }

namespace ckernel { unsigned *instrn_buffer; }
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
  WHERE_ROW ();
  WHERE_ROW ();
  WHERE_ROW ();
  WHERE_ROW ();
  WHERE_ROW ();
  WHERE_ROW ();
  WHERE_ROW ();
  WHERE_ROW ();
}
