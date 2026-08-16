// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-emit-loadmacro" }
// { dg-final { scan-assembler-not "SFPLOADMACRO" } }
// { dg-final { scan-assembler-not "SFPCONFIG" } }
// Without the per-row typed Dst += 2 there is no counter effect the
// auto-increment address mode could absorb; refuse byte-identically to OFF.
// { dg-final { scan-assembler-not "TTINCRWC" } }
// { dg-final { scan-assembler-times "SFPSWAP" 1 } }
// { dg-final { scan-assembler-times "SFPSTORE" 1 } }

#define MINMAX_OMIT_INCRWC
#include "loadmacro-periodic-minmax-near-miss-body.h"
