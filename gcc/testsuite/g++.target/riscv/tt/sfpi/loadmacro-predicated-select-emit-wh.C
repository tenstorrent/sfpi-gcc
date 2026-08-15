// { dg-options "-mcpu=tt-wh-tensix -O3 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-emit-loadmacro" }
// { dg-final { scan-assembler-times "\\.ttinsn" 3 } }
// { dg-final { scan-assembler-times {\.ttinsn\t2466381824} 1 } }
// { dg-final { scan-assembler-times {\.ttinsn\t2470838336} 1 } }
// { dg-final { scan-assembler-times {\.ttinsn\t2475032704} 1 } }
// { dg-final { scan-assembler-times "SFPCONFIG" 6 } }
// { dg-final { scan-assembler-times "TTINCRWC" 1 } }
// { dg-final { scan-assembler-not "SFPSETCC" } }
// { dg-final { scan-assembler-not "SFPSTORE" } }

#include "loadmacro-predicated-select-emit.inc"
