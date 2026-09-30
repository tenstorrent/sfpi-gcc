// Init-hoist near-miss: the derived-intmul row (the MulInt32 production
// shape) MUST refuse the upward search and keep its established ii=12
// formation byte-identically.  The refusals are structural, not tuned:
//  - every chain whose moved MUL24 does not read the VA=L0 factor
//    derives a template word that shares with nothing (the VA name is
//    packed in the word), overflowing the four-template budget;
//  - the in-place accumulate targets read their own destination (the
//    architectural SFPIADD VD tie) and refuse the split by name;
//  - variants that prove do so at no smaller interval.
// This is the recorded falsification of the pre-registered mulint32
// ii=11 target (docs/MACRO_PLANNER.md 2d).
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -mtt-tensix-macro-ims -mtt-tensix-macro-ims-carrier -fdump-rtl-rvtt_macro_planner" }
// { dg-final { scan-rtl-dump "Macro-planner upward-carrier-refusal: ims-carrier-rederive-unproven" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner upward-carrier-refusal: ims-carrier-legality-unproven" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner upward-carrier-refusal: ims-carrier-no-improvement" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "Macro-planner upward-carrier: formed" "rvtt_macro_planner" } }
// The established derived calendar commits untouched (the derived-intmul
// expectations, unchanged under both flags):
// { dg-final { scan-rtl-dump-times "Macro-planner formed: rows=8 runs=1" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner descriptor-word dest=0: 0x900000c3" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner descriptor-word dest=1: 0x94fe90d6" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner descriptor-word dest=2: 0x980009e0" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner descriptor-word dest=3: 0x900003f3" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2466570240" 8 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2471813184" 8 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2477047808" 8 } }

#include "macro-planner-ims-intmul-row.h"
