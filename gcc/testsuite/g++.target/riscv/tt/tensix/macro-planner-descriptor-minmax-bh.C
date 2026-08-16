// Descriptor synthesis reproduces the five frozen Min/Max words from the
// derived structure: SFPSWAP template fields packed from the admitted
// source (routing mod 1 because the FIRST result set reaches the store),
// the proven whole-word transient copy, the two proven sequence words,
// the misc word, the three owned SETC16 slot programs, and the launch
// tuples (alternating VD pair for macro 0, staging VD 3 for the delayed
// store, Dst += 2 absorbed).  The in-tree verifier decodes every word
// back and passes.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner-analyze -mtt-tensix-macro-planner-verify -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump-times "Macro-planner descriptor: templates=2 seq=2 misc=0x00000330 setc16=3 launches=2 drain=3 planned-lregs=0xf prefix=all-lanes" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner descriptor-word dest=0: 0x920002c1" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner descriptor-word dest=1: 0x940000d6" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner descriptor-word dest=4: 0x00dd008c" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner descriptor-word dest=5: 0x53000000" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner descriptor-word dest=8: 0x00000330" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner descriptor-setc16: 0xb2120000" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner descriptor-setc16: 0xb2220002" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner descriptor-setc16: 0xb2350000" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner descriptor-launch: macro=0 vd=0 word=0x9300e000 alt-vd=1 alt-word=0x9310e000" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner descriptor-launch: macro=1 vd=3 word=0x9370c080" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner verify: ok" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "descriptor-refusal" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-not "SFPLOADMACRO" } }
// { dg-final { scan-assembler-not "SFPCONFIG" } }

#include "loadmacro-periodic-minmax-body.h"
