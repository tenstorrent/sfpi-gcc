// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }
// Check that without -mtt-tensix-optimize-ccmask the compare-and-enable CC sequence is kept.
// { dg-final { scan-assembler-not "SFPGT" } }
// { dg-final { scan-assembler "SFPLE" } }
// { dg-final { scan-assembler "SFPENCC" } }
#define CCMASK_FN ccmask_defoff
#define CCMASK_X x
#define CCMASK_Y y
#define CCMASK_W w
#define CCMASK_A 0.4375f
#define CCMASK_B 1.5f
#include "ccmask-body.h"
