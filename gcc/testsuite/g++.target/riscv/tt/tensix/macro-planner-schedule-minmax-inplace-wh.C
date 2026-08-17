// Wormhole in-place store: candidate 0 (shared carrier) refuses by
// latency and descriptor-program proof; the deterministic stores-demoted
// fallback re-derives the proven three-slot calendar (single-slot
// Base=1 SETC16, sfpi-gcc 2a0ba1e6602).
// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner-analyze -mtt-tensix-macro-planner-verify -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump-times "Macro-planner schedule: ii=2 issues=2 launches=1 explicit=1 launched-events=2 vd=alternating drain=1" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner issue 0: launch macro=0 carries=load\\+store hosted=2 absorbs-stride=2" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner schedule-refusal: latency-violation" 1 "rvtt_macro_planner" } }
// (Since WP10 a schedule that names its own blocker never reaches
// descriptor synthesis -- except the documented event-delay-unproven
// carve-out below -- so the latency-refused candidate produces no
// descriptor-refusal line.)
// { dg-final { scan-rtl-dump-not "Macro-planner descriptor-refusal" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner schedule-candidate: stores-demoted" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner schedule: ii=3 issues=3 launches=2 explicit=1 launched-events=2 vd=alternating drain=unproven" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner issue 0: launch macro=0 carries=load hosted=1" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner issue 2: launch macro=1 carries=store hosted=1 absorbs-stride=2" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner issue 1: explicit subunit=load" 2 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner schedule-refusal: event-delay-unproven" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner descriptor: templates=2 seq=2 misc=0x00000330 setc16=3 launches=2 drain=3 planned-lregs=0xf prefix=all-lanes" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner descriptor-launch: macro=1 vd=3 word=0x93708000" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner verify: ok" 1 "rvtt_macro_planner" } }
// { dg-final { scan-assembler-not "SFPLOADMACRO" } }
// { dg-final { scan-assembler-not "SFPCONFIG" } }

#include "loadmacro-periodic-minmax-inplace-body.h"
