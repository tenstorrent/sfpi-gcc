// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-ccmask -fdump-tree-rvtt_ccmask" }
// A predicated zeroing under `x <= 0.0f` folds to the keep-mask
// compare (SFPGT SET_DEST against the constant-zero register) and an
// AND merge; the CC scaffolding (SETCC pair, COMPC, predicated move,
// ENCC) disappears from the row.
// { dg-final { scan-tree-dump-times "ccmask: folded zeroing CC region" 1 "rvtt_ccmask" } }
// { dg-final { scan-assembler-times "SFPGT\tL\[0-7\], L9, 0, 8" 1 } }
// { dg-final { scan-assembler "SFPAND" } }
// { dg-final { scan-assembler-not "SFPSETCC" } }
// { dg-final { scan-assembler-not "SFPCOMPC" } }
// { dg-final { scan-assembler-not "SFPENCC" } }
#define CCMASK_FN ccmask_fire
#define CCMASK_X x
#define CCMASK_Y y
#define CCMASK_W w
#define CCMASK_A 0.4375f
#define CCMASK_B 1.5f
#include "ccmask-body.h"
