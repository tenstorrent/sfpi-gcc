#include "rtl-lane-reset.h"

// Check that a forced verifier mismatch is named and prevents formation.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -mtt-tensix-macro-planner-verify-corrupt-template -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump "Macro-planner refusal: descriptor-verification-failed \\(template-mismatch\\)" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "Macro-planner verify: ok" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "Macro-planner formed" "rvtt_macro_planner" } }
// { dg-final { scan-assembler "SFPSWAP" } }
// { dg-final { scan-assembler "SFPSTORE" } }
// { dg-final { scan-assembler-not {SFPCONFIG[ \t]+([0-9]|1[0-4]),} } }
// { dg-final { scan-assembler-not "SFPLOADMACRO" } }

#include "loadmacro-periodic-minmax-body.h"
