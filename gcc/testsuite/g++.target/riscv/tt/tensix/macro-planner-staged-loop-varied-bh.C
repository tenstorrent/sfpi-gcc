#include "rtl-lane-reset.h"

// Check that a -15 shift packs its own imm12 into the template (hi16 0x94ff).
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner" }
// { dg-final { scan-assembler-times "SFPCONFIG" 5 } }
// { dg-final { scan-assembler-times "SFPLOADI\\tL0, 38143, 8" 1 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2467348480" 1 } }
// { dg-final { scan-assembler-times "SFPNOP" 3 } }
// { dg-final { scan-assembler-not "SFPSHFT" } }
// { dg-final { scan-assembler-not "TTINCRWC" } }

#define STAGED_SHIFT -15
#include "macro-planner-staged-loop-body.h"
