// WP9 genericity: varied-constant twin of the select formation --
// different Dst addresses, a different payload/store data mode, and
// the COMPLEMENTED source predicate sense (setcc mod 6 = EQ0, so the
// template takes the direct complement 2 = NE0).  Everything
// re-derives; no value fingerprint participates anywhere.  Since WP10:
// this twin's modes are UNIFORM (condition mode == payload/store mode
// 2), so the compact 3-slot calendar proves first -- misc is the
// proven launch-sourced-mod0 word 0x770, the second launch carries the
// first payload, and the trailing payload load absorbs the stride
// (mixed-mode twins keep the established 4-slot calendar; see
// macro-planner-select-form-bh.C).
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump "Macro-planner schedule-candidate: cc-compact" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner schedule: ii=3 issues=3 launches=2 explicit=1" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner descriptor-cc: sense=complement def-visible=2 pre-load=1 post-load=2 store-exec=3 restore-visible=3 interval=3 separator=absorbed" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner descriptor-word dest=0: 0x7b0000c2" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner descriptor-word dest=1: 0x8a0000d0" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner descriptor-word dest=8: 0x00000770" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner descriptor-launch: macro=0 vd=0 word=0x9302e010" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner descriptor-launch: macro=1 vd=0 word=0x9342e060" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner verify: ok" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner formed: rows=8 runs=1" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-not "SFPSETCC" } }
// { dg-final { scan-assembler-not "TTINCRWC" } }
// { dg-final { scan-assembler-times "SFPLOAD\\tL0, 192, 2, 6" 8 } }

#define SELECT_COND_ADDR 16
#define SELECT_TRUE_ADDR 96
#define SELECT_FALSE_ADDR 192
#define SELECT_COND_MODE 2
#define SELECT_PAYLOAD_MODE 2
#define SELECT_SETCC_MOD 6
#define SELECT_ADDR_MODE 7
#include "macro-planner-select-body.h"

__attribute__((noinline)) void select_rows_varied ()
{
  SELECT_ROWS_8 ();
}
