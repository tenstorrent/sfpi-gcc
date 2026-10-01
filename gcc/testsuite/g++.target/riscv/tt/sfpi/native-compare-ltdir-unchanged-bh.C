// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-native-compare" }
// Direction control: strict-less is the reversed native greater-than compare;
// the compatibility flag leaves that established lowering untouched.
// { dg-final { scan-assembler "SFPGT\tL9, L1" } }
// { dg-final { scan-assembler-not "SFPLE" } }
// { dg-final { scan-assembler "SFPENCC" } }
#define NC_FN nc_ltdir
#define NC_COND(x) ((x) < 0.0f)
#define NC_X x
#define NC_Y y
#define NC_A 0.4375f
#define NC_B 1.5f
#include "native-compare-body.h"
