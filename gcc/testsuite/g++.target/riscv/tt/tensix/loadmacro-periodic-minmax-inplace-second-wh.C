// Wormhole in-place store, second select result (mod-9 routing through
// the store-demotion fallback).  Config prefix is the corrected
// single-slot Base=1 SETC16 program (three words, not the frozen
// pass's six -- sfpi-gcc 2a0ba1e6602), so .ttinsn is 19.
// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner" }
// { dg-final { scan-assembler-times "SFPCONFIG" 5 } }
// { dg-final { scan-assembler-times "\\.ttinsn" 19 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2473623552" 8 } }
// { dg-final { scan-assembler-times {SFPLOADI\tL0, 713, 2} 1 } }
// { dg-final { scan-assembler-times "SFPLOAD\\t" 8 } }
// { dg-final { scan-assembler-times "SFPNOP" 3 } }
// { dg-final { scan-assembler-not "SFPSWAP" } }
// { dg-final { scan-assembler-not "SFPSTORE" } }
// { dg-final { scan-assembler-not "TTINCRWC" } }

#define RESULT_INDEX 1
#include "loadmacro-periodic-minmax-inplace-body.h"
