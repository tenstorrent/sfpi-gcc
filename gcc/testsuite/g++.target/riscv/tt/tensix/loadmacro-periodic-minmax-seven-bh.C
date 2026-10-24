#include "rtl-lane-reset.h"

// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner" }
// Check that seven min/max rows form a macro on Blackhole.
// { dg-final { scan-assembler-times "SFPENCC" 2 } }
// { dg-final { scan-assembler-times "SFPCONFIG" 6 } }
// { dg-final { scan-assembler-times "\\.ttinsn" 17 } }
// { dg-final { scan-assembler-times "SFPLOAD\t" 7 } }
// { dg-final { scan-assembler-times "SFPNOP" 3 } }
// { dg-final { scan-assembler-not "SFPSWAP" } }
// { dg-final { scan-assembler-not "SFPSTORE" } }
// { dg-final { scan-assembler-not "TTINCRWC" } }

#define MINMAX_SEVEN_BODY
#include "loadmacro-periodic-minmax-body.h"
