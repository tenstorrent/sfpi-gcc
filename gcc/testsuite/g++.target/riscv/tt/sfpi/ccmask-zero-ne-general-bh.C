// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-ccmask -mtt-tensix-optimize-cc-region-general -fdump-tree-rvtt_ccmask" }
// R2 widening 2 fire, NE direction (tt/proofs/ccmask-eqne-zero/):
// under the stage-B flag the predicated zeroing under `x != 0.0f`
// folds to SFPAND (SFPLE (x, 0), SFPLE (0, x)) -- keep exactly the
// raw-bit +0 encoding -- and the AND merge; the CC scaffolding
// disappears.  Two ANDs total: the mask combine and the value merge.
// { dg-final { scan-tree-dump-times "ccmask: folded zeroing CC region" 1 "rvtt_ccmask" } }
// { dg-final { scan-assembler-times "SFPLE" 2 } }
// { dg-final { scan-assembler-times "SFPAND" 2 } }
// { dg-final { scan-assembler-not "SFPSETCC" } }
#define CCMASK_FN ccmask_ne_general
#define CCMASK_COND(x) ((x) != 0.0f)
#define CCMASK_X x
#define CCMASK_Y y
#define CCMASK_W w
#define CCMASK_A 0.4375f
#define CCMASK_B 1.5f
#include "ccmask-body.h"
