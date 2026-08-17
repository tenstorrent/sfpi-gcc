// WP9: the canonical SFPI where selector (the TTNN Where kernel's v_if
// spelling), eight rows, forms through the generic planner: the
// outermost-CC combine lowers each v_if to SETCC/predicated-MOV/ENCC,
// the planner derives the select CC-template descriptor, and the eight
// rows become two launches plus an explicit payload load each.
// { dg-options "-mcpu=tt-bh-tensix -O3 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump "Macro-planner descriptor-cc: sense=complement" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner descriptor-word dest=0: 0x7b0000c6" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner descriptor-word dest=1: 0x8a0000d0" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner descriptor-word dest=8: 0x00000706" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner verify: ok" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner formed: rows=8 runs=1" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-times "\\.ttinsn" 16 } }
// { dg-final { scan-assembler-not "SFPSETCC" } }
// { dg-final { scan-assembler-not "SFPMOV" } }
// { dg-final { scan-assembler-not "SFPSTORE" } }
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
