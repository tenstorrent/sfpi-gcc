// WP10 compact CC calendar: a UNIFORM-mode predicated select (the
// condition's data mode equals the payload/store mode, here 6) forms
// the production handwritten Where protocol's own 3-slot row: the
// definition carrier hosts the SETCC template and the delayed store
// (launch-sourced store mod0, proven whole misc word 0x770), the
// SECOND launch carries the first payload and hosts the all-lanes
// restore, and the trailing payload load stays explicit and absorbs
// the row stride through its own auto-increment address mode -- the
// typed separator is deleted, one issue slot per row cheaper than the
// established 4-slot calendar.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump "Macro-planner schedule-candidate: cc-compact" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner schedule: ii=3 issues=3 launches=2 explicit=1 launched-events=3" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner issue 2: explicit subunit=load absorbs-stride" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner descriptor-cc: sense=complement def-visible=2 pre-load=1 post-load=2 store-exec=3 restore-visible=3 interval=3 separator=absorbed" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner descriptor-word dest=0: 0x7b0000c6" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner descriptor-word dest=1: 0x8a0000d0" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner descriptor-word dest=4: 0x13000004" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner descriptor-word dest=5: 0x00000005" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner descriptor-word dest=8: 0x00000770" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner descriptor-launch: macro=0 vd=0 word=0x9306e000" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner descriptor-launch: macro=1 vd=0 word=0x9346e020" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner verify: ok" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner formed: rows=8 runs=1" "rvtt_macro_planner" } }
// Formed body: two launches and the auto-increment explicit payload
// load per row (address mode 6 = the owned Dst += 2 slot); the typed
// separator is gone.
// { dg-final { scan-assembler-not "SFPSETCC" } }
// { dg-final { scan-assembler-not "SFPSTORE" } }
// { dg-final { scan-assembler-not "TTINCRWC" } }
// { dg-final { scan-assembler-times "SFPLOAD\\tL0, 64, 6, 6" 8 } }

#define SELECT_ADDR_MODE 7
#define SELECT_COND_MODE 6
#include "macro-planner-select-body.h"

__attribute__((noinline)) void select_uniform_compact ()
{
  __builtin_rvtt_sfppushc (0);
  __builtin_rvtt_sfppopc (0);
  SELECT_ROWS_8 ();
}
