// { dg-options "-mcpu=tt-wh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-native-compare" }
// Target control: SFPGT/SFPLE do not exist on Wormhole -- the flag is
// inert there and the established SETCC web is kept byte-identically.
// { dg-final { scan-assembler-not "SFPGT" } }
// { dg-final { scan-assembler-not "SFPLE" } }
// { dg-final { scan-assembler "SFPSETCC" } }
#define NC_FN nc_wh_control
#define NC_COND(x) ((x) > 0.0f)
#define NC_X x
#define NC_Y y
#define NC_A 0.4375f
#define NC_B 1.5f
#include "native-compare-body.h"
