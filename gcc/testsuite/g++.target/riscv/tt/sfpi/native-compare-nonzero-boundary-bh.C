// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-native-compare" }
// Non-zero boundary (x > 0.4375f): the established MAD subtract stays
// and the difference is compared natively -- the proof covers every
// emitted spelling because both fcmp emitters reduce to compare-vs-+0.
// { dg-final { scan-assembler-times "SFPGT\tL\[0-7\], L9, 0, 1" 1 } }
// { dg-final { scan-assembler "SFPMAD" } }
// { dg-final { scan-assembler-not "SFPSETCC" } }
#define NC_FN nc_gt_boundary
#define NC_COND(x) ((x) > 0.4375f)
#define NC_X x
#define NC_Y y
#define NC_A 0.4375f
#define NC_B 1.5f
#include "native-compare-body.h"
