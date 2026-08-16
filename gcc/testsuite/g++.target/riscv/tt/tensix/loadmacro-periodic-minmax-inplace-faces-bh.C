// The production kernel shape: rows=32 runs=4 (four eight-row face runs
// separated by the typed architectural face advance), each row storing
// IN PLACE to its first load's Dst address.  One materialized descriptor
// shared across all four runs; the store-demotion fallback re-derives
// the frozen calendar.  Frozen-pass byte-parity re-verified offline on
// this same source.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner" }
// { dg-final { scan-assembler-times "SFPENCC" 1 } }
// { dg-final { scan-assembler-times "SFPCONFIG" 5 } }
// { dg-final { scan-assembler-times "\\.ttinsn" 67 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2466308096" 16 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2467356672" 16 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2473639936" 32 } }
// { dg-final { scan-assembler-times "SFPLOAD\\t" 32 } }
// { dg-final { scan-assembler-times "SFPNOP" 12 } }
// { dg-final { scan-assembler-times {TTSETRWC\t0, 4, 8, 0, 0, 4} 6 } }
// { dg-final { scan-assembler-not "SFPSWAP" } }
// { dg-final { scan-assembler-not "SFPSTORE" } }
// { dg-final { scan-assembler-not "TTINCRWC" } }

#define MINMAX_FOUR_FACE_RUNS 1
#include "loadmacro-periodic-minmax-inplace-body.h"
