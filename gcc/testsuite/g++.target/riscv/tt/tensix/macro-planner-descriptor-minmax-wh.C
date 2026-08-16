// WH synthesis: identical five descriptor words, the six-word dual-slot
// SETC16 program (the unencoded Base selector can map the launch's index
// to either physical bank), and WH launch encodings.
// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner-analyze -mtt-tensix-macro-planner-verify -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump-times "Macro-planner descriptor: templates=2 seq=2 misc=0x00000330 setc16=6 launches=2 drain=3 planned-lregs=0xf prefix=all-lanes" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner descriptor-word dest=0: 0x920002c1" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner descriptor-setc16: 0xb20b0000" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner descriptor-setc16: 0xb2190002" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner descriptor-setc16: 0xb2320000" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner descriptor-setc16: 0xb2130000" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner descriptor-setc16: 0xb21d0002" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner descriptor-setc16: 0xb2360000" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner descriptor-launch: macro=0 vd=0 word=0x9300c000 alt-vd=1 alt-word=0x9310c000" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner descriptor-launch: macro=1 vd=3 word=0x93708080" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner verify: ok" 1 "rvtt_macro_planner" } }

#include "loadmacro-periodic-minmax-body.h"
