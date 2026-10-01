// Six rows do not amortize the owned prefix and drain calendar on
// Wormhole -- the new WH break-even boundary after the single-slot
// correction (sfpi-gcc 2a0ba1e6602): seven forms
// (loadmacro-periodic-minmax-seven-wh.C), six refuses, mirroring BH's
// boundary (loadmacro-periodic-minmax-six-bh.C).
// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner" }
// { dg-final { scan-assembler-not "SFPLOADMACRO" } }
// { dg-final { scan-assembler-not "SFPCONFIG" } }
// { dg-final { scan-assembler-times "SFPSWAP" 1 } }
// { dg-final { scan-assembler-times "SFPSTORE" 1 } }
// { dg-final { scan-assembler-times "TTINCRWC" 6 } }

#define MINMAX_SIX_BODY
#include "loadmacro-periodic-minmax-body.h"
