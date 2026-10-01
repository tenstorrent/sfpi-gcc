// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner" }
// { dg-final { scan-assembler-not "SFPLOADMACRO" } }
// { dg-final { scan-assembler-not "SFPCONFIG" } }
// Configuration materialization is lane-predicated; without the local
// all-lanes CC proof (SFPENCC) the rows must refuse byte-identically to OFF.
// { dg-final { scan-assembler-not "SFPENCC" } }
// { dg-final { scan-assembler-times "SFPSWAP" 1 } }
// { dg-final { scan-assembler-times "SFPSTORE" 1 } }
// { dg-final { scan-assembler-times "TTINCRWC" 8 } }

#define MINMAX_OMIT_ENABLE
#include "loadmacro-periodic-minmax-near-miss-body.h"
