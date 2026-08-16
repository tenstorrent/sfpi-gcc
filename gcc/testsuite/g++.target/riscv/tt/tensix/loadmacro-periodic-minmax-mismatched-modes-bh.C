// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-emit-loadmacro" }
// { dg-final { scan-assembler-not "SFPLOADMACRO" } }
// { dg-final { scan-assembler-not "SFPCONFIG" } }
// The launch mode field is shared by both loads and the store; rows whose
// loads disagree on the data mode must refuse byte-identically to OFF.
// { dg-final { scan-assembler-times "SFPSWAP" 1 } }
// { dg-final { scan-assembler-times "SFPSTORE" 1 } }
// { dg-final { scan-assembler-times "TTINCRWC" 8 } }

#define MINMAX_LOAD1_MODE 2
#include "loadmacro-periodic-minmax-near-miss-body.h"
