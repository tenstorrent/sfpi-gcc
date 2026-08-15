// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-emit-loadmacro" }
// { dg-final { scan-assembler-not "SFPLOADMACRO" } }
// { dg-final { scan-assembler-not "SFPCONFIG" } }
// Six rows do not amortize the owned prefix and drain calendar on BH.
// { dg-final { scan-assembler-times "SFPSWAP" 1 } }
// { dg-final { scan-assembler-times "SFPSTORE" 1 } }
// { dg-final { scan-assembler-times "TTINCRWC" 6 } }

#define MINMAX_SIX_BODY
#include "loadmacro-periodic-minmax-body.h"
