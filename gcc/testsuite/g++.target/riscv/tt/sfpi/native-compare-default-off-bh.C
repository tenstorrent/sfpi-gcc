// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }
// Current SFPI lowers the comparison directly; the obsolete optional pass is
// not needed for this established native compare-and-enable sequence.
// { dg-final { scan-assembler "SFPGT\tL1, L9" } }
// { dg-final { scan-assembler-not "SFPLE" } }
// { dg-final { scan-assembler "SFPENCC" } }
#define NC_FN nc_defoff
#define NC_COND(x) ((x) > 0.0f)
#define NC_X x
#define NC_Y y
#define NC_A 0.4375f
#define NC_B 1.5f
#include "native-compare-body.h"
