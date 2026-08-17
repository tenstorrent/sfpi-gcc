// WP9 CC-template extension: the predicated-select (TTNN Where class)
// forms through the generic planner.  The derived calendar is two
// launches plus an explicit payload load per row: the definition
// carrier hosts the SETCC template (sense COMPLEMENTED because the
// post-visibility load carries the merge's live operand) and the
// delayed all-lanes store; the last carrier hosts the all-lanes-restore
// ENCC template; the lane-merge SFPMOV is coalesced into the shared
// launch VD by the deferred-CC dataflow; the explicit typed separator
// stays as the restore-visibility slot.  Every descriptor word below is
// re-derived from tables and admitted operands -- the frozen select
// protocol reproduced without its calendar.
// Since WP10 this MIXED-mode shape (condition mode 2, payload/store
// mode 6) is also the compact near-miss: the compact 3-slot candidate
// is tried first and its descriptor refuses by name -- launch-sourced
// store mod0 obliges the definition carrier's mode to equal the store
// mode -- so the established 4-slot calendar below forms unchanged.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump "Macro-planner region: rows=8 row-len=7 runs=1 stride=2" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner schedule-candidate: cc-compact" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner descriptor-refusal: cc-template-unproved" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner schedule: ii=4 issues=4 launches=2 explicit=2 launched-events=3" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner descriptor-cc: sense=complement def-visible=2 pre-load=1 post-load=2 store-latch=0 restore-visible=4 interval=4 separator=kept" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner descriptor-word dest=0: 0x7b0000c6" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner descriptor-word dest=1: 0x8a0000d0" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner descriptor-word dest=4: 0x13000004" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner descriptor-word dest=5: 0x00000005" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner descriptor-word dest=8: 0x00000706" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner descriptor-launch: macro=0 vd=0 word=0x9302e000" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner descriptor-launch: macro=1 vd=0 word=0x9346e040" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner verify: ok" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner formed: rows=8 runs=1" "rvtt_macro_planner" } }
// Formed body: launches as raw words, one explicit payload load and the
// kept separator per row, the proven drain; the predicate write, merge,
// restore, and store mnemonics are gone.
// { dg-final { scan-assembler-times "\\.ttinsn" 16 } }
// { dg-final { scan-assembler-times "SFPLOAD\tL0, 32" 8 } }
// { dg-final { scan-assembler-times "TTINCRWC" 8 } }
// { dg-final { scan-assembler-times "SFPENCC" 1 } }
// { dg-final { scan-assembler-not "SFPSETCC" } }
// { dg-final { scan-assembler-not "SFPMOV" } }
// { dg-final { scan-assembler-not "SFPSTORE" } }

#define SELECT_ADDR_MODE 7
#include "macro-planner-select-body.h"

__attribute__((noinline)) void select_rows ()
{
  SELECT_ROWS_8 ();
}
