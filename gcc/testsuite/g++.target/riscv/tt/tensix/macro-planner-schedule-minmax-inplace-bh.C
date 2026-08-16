// In-place store (store Dst address == first load's): maximal carrier
// sharing (candidate 0) merges the store into the load's launch carrier,
// which both violates the latency model and proves no descriptor
// program.  The deterministic fallback (candidate 1, stores-demoted)
// then re-derives the three-slot alternating-VD calendar -- launch 0
// carries the first load and hosts the launched simple event, the second
// load issues explicitly, launch 1 carries the delayed store and absorbs
// the typed Dst stride -- whose descriptor program is proven, so the
// planner commits it.  Nothing keys on the address value itself: the
// same search visits the distinct-address shape's single candidate.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner-analyze -mtt-tensix-macro-planner-verify -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump-times "Macro-planner schedule: ii=2 issues=2 launches=1 explicit=1 launched-events=2 vd=alternating drain=1" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner issue 0: launch macro=0 carries=load\\+store hosted=2 absorbs-stride=2" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner schedule-refusal: latency-violation" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner descriptor-refusal: descriptor-program-unproven" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner schedule-candidate: stores-demoted" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner schedule: ii=3 issues=3 launches=2 explicit=1 launched-events=2 vd=alternating drain=unproven" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner issue 0: launch macro=0 carries=load hosted=1" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner issue 2: launch macro=1 carries=store hosted=1 absorbs-stride=2" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner issue 1: explicit subunit=load" 2 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner schedule-refusal: event-delay-unproven" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner descriptor: templates=2 seq=2 misc=0x00000330 setc16=3 launches=2 drain=3 planned-lregs=0xf prefix=all-lanes" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner descriptor-launch: macro=1 vd=3 word=0x9370c000" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner verify: ok" 1 "rvtt_macro_planner" } }
// { dg-final { scan-assembler-not "SFPLOADMACRO" } }
// { dg-final { scan-assembler-not "SFPCONFIG" } }

#include "loadmacro-periodic-minmax-inplace-body.h"
