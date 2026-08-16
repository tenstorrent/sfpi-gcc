// In-place store, second select result: the dataflow-selected mod-9
// routing (second SET reaches the store) must survive the store-demotion
// fallback -- template word SFPLOADI immediate 713 = 705 | routing bit.
// Frozen-pass byte-parity re-verified offline on this same source.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner" }
// { dg-final { scan-assembler-times "SFPCONFIG" 5 } }
// { dg-final { scan-assembler-times "\\.ttinsn" 19 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2473639936" 8 } }
// { dg-final { scan-assembler-times {SFPLOADI\tL0, 713, 2} 1 } }
// { dg-final { scan-assembler-times "SFPLOAD\\t" 8 } }
// { dg-final { scan-assembler-times "SFPNOP" 3 } }
// { dg-final { scan-assembler-not "SFPSWAP" } }
// { dg-final { scan-assembler-not "SFPSTORE" } }
// { dg-final { scan-assembler-not "TTINCRWC" } }

#define RESULT_INDEX 1
#include "loadmacro-periodic-minmax-inplace-body.h"
