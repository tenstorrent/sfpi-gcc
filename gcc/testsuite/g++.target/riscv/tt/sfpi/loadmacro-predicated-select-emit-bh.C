// { dg-options "-mcpu=tt-bh-tensix -O3 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-emit-loadmacro -fdump-rtl-rvtt_loadmacro-details" }
// { dg-final { scan-assembler-times "\\.ttinsn" 3 } }
// { dg-final { scan-assembler-times {\.ttinsn\t2466381824} 1 } }
// { dg-final { scan-assembler-times {\.ttinsn\t2470838336} 1 } }
// { dg-final { scan-assembler-times {\.ttinsn\t2475032704} 1 } }
// { dg-final { scan-assembler-times "SFPCONFIG" 6 } }
// { dg-final { scan-assembler-times {SFPCONFIG\t0} 1 } }
// { dg-final { scan-assembler-times {SFPCONFIG\t1} 1 } }
// { dg-final { scan-assembler-times {SFPCONFIG\t4} 1 } }
// { dg-final { scan-assembler-times {SFPCONFIG\t5} 1 } }
// { dg-final { scan-assembler-times {SFPCONFIG\t6} 1 } }
// { dg-final { scan-assembler-times {SFPCONFIG\t8} 1 } }
// { dg-final { scan-assembler-times "TTINCRWC" 1 } }
// { dg-final { scan-assembler-not "SFPSETCC" } }
// { dg-final { scan-assembler-not "SFPSTORE" } }
// { dg-final { scan-rtl-dump-times "descriptor=predicated-three-load-select-store .*misc=706" 1 "rvtt_loadmacro" } }

#include "loadmacro-predicated-select-emit.inc"
