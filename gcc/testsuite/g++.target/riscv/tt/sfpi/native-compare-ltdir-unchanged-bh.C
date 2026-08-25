// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-native-compare" }
// Direction control: strict-less already lowers to a single SETCC --
// the flag leaves the LT/GE directions byte-identically untouched.
// { dg-final { scan-assembler-not "SFPGT" } }
// { dg-final { scan-assembler-not "SFPLE" } }
// { dg-final { scan-assembler "SFPSETCC" } }
#define NC_FN nc_ltdir
#define NC_COND(x) ((x) < 0.0f)
#define NC_X x
#define NC_Y y
#define NC_A 0.4375f
#define NC_B 1.5f
#include "native-compare-body.h"
