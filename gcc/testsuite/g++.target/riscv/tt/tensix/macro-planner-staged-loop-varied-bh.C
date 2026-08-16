// Varied-constant proof (WP8 genericity self-check): a shift of -15
// instead of -31 forms with the imm12 field packed from the typed
// immediate (template hi16 0x94ff instead of 0x94fe) -- formation is
// keyed by encodability, never by the -31 fingerprint.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner" }
// { dg-final { scan-assembler-times "SFPCONFIG" 4 } }
// { dg-final { scan-assembler-times "SFPLOADI\\tL0, 38143, 8" 1 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2467348480" 1 } }
// { dg-final { scan-assembler-times "SFPNOP" 3 } }
// { dg-final { scan-assembler-not "SFPSHFT" } }
// { dg-final { scan-assembler-not "TTINCRWC" } }

#define STAGED_SHIFT -15
#include "macro-planner-staged-loop-body.h"
