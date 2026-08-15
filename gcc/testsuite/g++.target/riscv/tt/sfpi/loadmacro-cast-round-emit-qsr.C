// { dg-options "-mcpu=tt-qsr32-tensix -O3 -fno-exceptions -fno-rtti -mtt-tensix-emit-loadmacro" }
// { dg-final { scan-assembler-not "SFPCONFIG" } }
// { dg-final { scan-assembler-not "\\.ttinsn" } }
// { dg-final { scan-assembler-times "TTINCRWC" 8 } }
// { dg-final { scan-assembler-times "TTREPLAY" 9 } }
// { dg-final { scan-assembler-times "SFPLOAD" 1 } }
// { dg-final { scan-assembler-times "SFPCAST" 1 } }
// { dg-final { scan-assembler-times "SFPSTOCH" 1 } }
// { dg-final { scan-assembler-times "SFPSTORE" 1 } }

#include "loadmacro-cast-round-emit.inc"
