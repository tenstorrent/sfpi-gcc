// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-emit-loadmacro" }
// { dg-final { scan-assembler-not "SFPLOADMACRO" } }
// { dg-final { scan-assembler-not "SFPCONFIG" } }
// A Dst address above 0x3ff does not fit the launch address field; the
// otherwise-exact eight-row body must stay explicit, byte-identical to OFF.
// { dg-final { scan-assembler-times "SFPSWAP" 1 } }
// { dg-final { scan-assembler-times "SFPSTORE" 1 } }
// { dg-final { scan-assembler-times "TTINCRWC" 8 } }

#define MINMAX_STORE_ADDR 1024
#include "loadmacro-periodic-minmax-near-miss-body.h"
