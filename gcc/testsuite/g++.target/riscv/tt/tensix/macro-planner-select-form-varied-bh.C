// WP9 genericity: varied-constant twin of the select formation --
// different Dst addresses, a different payload/store data mode (2, so
// the field-derived misc becomes 0x702), and the COMPLEMENTED source
// predicate sense (setcc mod 6 = EQ0, so the template takes the direct
// complement 2 = NE0).  Everything re-derives; no value fingerprint
// participates anywhere.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump "Macro-planner descriptor-cc: sense=complement" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner descriptor-word dest=0: 0x7b0000c2" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner descriptor-word dest=1: 0x8a0000d0" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner descriptor-word dest=8: 0x00000702" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner descriptor-launch: macro=0 vd=0 word=0x9302e010" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner descriptor-launch: macro=1 vd=0 word=0x9342e0c0" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner verify: ok" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner formed: rows=8 runs=1" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-not "SFPSETCC" } }

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
