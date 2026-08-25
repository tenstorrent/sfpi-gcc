// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-native-compare" }
// Less-or-equal boundary compare (x <= 0.0f): the three-word SETCC web
// plus fenced COMPC is replaced by the single BH-native SFPLE SET_CC
// against the constant +0.0 register L9 (the complement is native).
// Proof artifact: tt/proofs/native-compare-gtle/ (EQUAL over 2^32).
// { dg-final { scan-assembler-times "SFPLE\tL\[0-7\], L9, 0, 1" 1 } }
// { dg-final { scan-assembler-not "SFPSETCC" } }
// { dg-final { scan-assembler-not "SFPCOMPC" } }
#define NC_FN nc_le_fire
#define NC_COND(x) ((x) <= 0.0f)
#define NC_X x
#define NC_Y y
#define NC_A 0.4375f
#define NC_B 1.5f
#include "native-compare-body.h"
