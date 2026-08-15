// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti" }
// { dg-final { scan-assembler-not "SFPLOADMACRO" } }
// { dg-final { scan-assembler-not "SFPCONFIG" } }
// { dg-final { scan-assembler-times "SFPSHFT" 1 } }
// { dg-final { scan-assembler-times "SFPCAST" 1 } }

#include "loadmacro-staged-body.h"
