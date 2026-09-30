// The Layer-7 verifier is consequence-bearing: a verification mismatch is
// a descriptor refusal that prevents form_region.  Force a mismatch with
// the testing-only fault-injection flag (the adversarial suite's
// routing-mod flip applied to the verifier's LOCAL word copy -- the
// descriptor itself is never corrupted): the mismatch must be named, the
// region must never form, and the output must be the byte-identical
// refusal (explicit SFPSWAP/SFPSTORE rows kept, no macro configuration).
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -mtt-tensix-macro-planner-verify-corrupt-template -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump "Macro-planner refusal: descriptor-verification-failed \\(template-mismatch\\)" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "Macro-planner verify: ok" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "Macro-planner formed" "rvtt_macro_planner" } }
// { dg-final { scan-assembler "SFPSWAP" } }
// { dg-final { scan-assembler "SFPSTORE" } }
// { dg-final { scan-assembler-not "SFPCONFIG" } }
// { dg-final { scan-assembler-not "SFPLOADMACRO" } }

#include "loadmacro-periodic-minmax-body.h"
