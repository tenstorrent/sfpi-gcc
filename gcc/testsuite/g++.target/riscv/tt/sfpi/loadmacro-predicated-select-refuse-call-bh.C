// { dg-options "-mcpu=tt-bh-tensix -O3 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-emit-loadmacro" }
// { dg-final { scan-assembler-not "\\.ttinsn" } }
// { dg-final { scan-assembler-not "SFPCONFIG" } }
// { dg-final { scan-assembler-times "SFPSTORE" 1 } }

#define SELECT_REFUSE_CALL 1
#define SELECT_INCREMENT 2
#include "loadmacro-predicated-select-refuse.inc"
