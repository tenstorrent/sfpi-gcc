// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-native-compare" }
// Strict-greater boundary compare (x > 0.0f): the WH-era two-word
// SETCC web (mod4 sign-clear + mod2 nonzero) is replaced by the single
// BH-native SFPGT SET_CC against the constant +0.0 register L9.
// Proof artifact: tt/proofs/native-compare-gtle/ (EQUAL over 2^32).
// { dg-final { scan-assembler-times "SFPGT\tL\[0-7\], L9, 0, 1" 1 } }
// { dg-final { scan-assembler-not "SFPSETCC" } }
// { dg-final { scan-assembler-not "SFPCOMPC" } }
#define NC_FN nc_gt_fire
#define NC_COND(x) ((x) > 0.0f)
#define NC_X x
#define NC_Y y
#define NC_A 0.4375f
#define NC_B 1.5f
#include "native-compare-body.h"
