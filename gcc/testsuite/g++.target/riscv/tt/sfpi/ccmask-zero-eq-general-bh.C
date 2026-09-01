// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-ccmask -mtt-tensix-optimize-cc-region-general -fdump-tree-rvtt_ccmask" }
// R2 widening 2 fire (FABLE_GOES_BURR; tt/proofs/ccmask-eqne-zero/):
// under the stage-B flag the predicated zeroing under `x == 0.0f`
// folds to the proven two-compare keep-mask composition
// SFPOR (SFPGT (x, 0), SFPGT (0, x)) -- the raw-bit EQ0 enable set is
// order-expressible compositionally even though no single order test
// expresses it -- and the AND merge; the CC scaffolding disappears.
// { dg-final { scan-tree-dump-times "ccmask: folded zeroing CC region" 1 "rvtt_ccmask" } }
// { dg-final { scan-assembler-times "SFPGT" 2 } }
// { dg-final { scan-assembler "SFPOR" } }
// { dg-final { scan-assembler "SFPAND" } }
// { dg-final { scan-assembler-not "SFPSETCC" } }
#define CCMASK_FN ccmask_eq_general
#define CCMASK_COND(x) ((x) == 0.0f)
#define CCMASK_X x
#define CCMASK_Y y
#define CCMASK_W w
#define CCMASK_A 0.4375f
#define CCMASK_B 1.5f
#include "ccmask-body.h"
