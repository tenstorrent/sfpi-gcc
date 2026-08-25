// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }
// Default-off control: without -mtt-tensix-optimize-native-compare the
// GT compare keeps the established SETCC web byte-identically.
// { dg-final { scan-assembler-not "SFPGT" } }
// { dg-final { scan-assembler-not "SFPLE" } }
// { dg-final { scan-assembler "SFPSETCC" } }
#define NC_FN nc_defoff
#define NC_COND(x) ((x) > 0.0f)
#define NC_X x
#define NC_Y y
#define NC_A 0.4375f
#define NC_B 1.5f
#include "native-compare-body.h"
