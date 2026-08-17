// WP9: Wormhole twin of macro-planner-select-form-bh.C.  The
// descriptor-state words are WH/BH-common; only the launch words differ
// (2-bit address-modifier field at bit 14, no-increment mode 3).
// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump "Macro-planner descriptor-cc: sense=complement def-visible=2 pre-load=1 post-load=2 store-latch=0 restore-visible=4 interval=4 separator=kept" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner descriptor-word dest=0: 0x7b0000c6" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner descriptor-word dest=1: 0x8a0000d0" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner descriptor-word dest=4: 0x13000004" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner descriptor-word dest=5: 0x00000005" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner descriptor-word dest=8: 0x00000706" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner descriptor-launch: macro=0 vd=0 word=0x9302c000" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner descriptor-launch: macro=1 vd=0 word=0x9346c040" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner verify: ok" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner formed: rows=8 runs=1" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-times "\\.ttinsn" 16 } }
// { dg-final { scan-assembler-times "SFPLOAD\tL0, 32" 8 } }
// { dg-final { scan-assembler-times "TTINCRWC" 8 } }
// { dg-final { scan-assembler-not "SFPSETCC" } }
// { dg-final { scan-assembler-not "SFPMOV" } }
// { dg-final { scan-assembler-not "SFPSTORE" } }

#define SELECT_ADDR_MODE 3
#include "macro-planner-select-body.h"

__attribute__((noinline)) void select_rows ()
{
  SELECT_ROWS_8 ();
}
