// Varied-constant proof: a different round instr_mod1 (2) packs the
// template field from the admitted operand (word low byte 0xd2, loadi
// 210) -- the field derivation, never the value 1, is the capability.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner" }
// { dg-final { scan-assembler-times "SFPCONFIG" 4 } }
// { dg-final { scan-assembler-times "SFPLOADI\\tL0, 210, 2" 1 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2466693120" 4 } }
// { dg-final { scan-assembler-times "SFPNOP" 3 } }
// { dg-final { scan-assembler-not "SFPSTOCHRND" } }
// { dg-final { scan-assembler-not "TTINCRWC" } }

#define CAST_ROUND_MOD1 2
#include "macro-planner-cast-round-group-body.h"
