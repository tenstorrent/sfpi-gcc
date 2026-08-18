// { dg-options "-mcpu=tt-wh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-ccmask -fdump-tree-rvtt_ccmask" }
// Target control: the mask equivalence is proven against the BH models
// and SFPGT/SFPLE do not exist before BH -- Wormhole refuses by name
// and keeps the CC lowering byte-identically.
// { dg-final { scan-tree-dump "ccmask refused .ccmask-target-unproven" "rvtt_ccmask" } }
// { dg-final { scan-assembler-not "SFPGT" } }
// { dg-final { scan-assembler "SFPSETCC" } }
#define CCMASK_FN ccmask_wh
#define CCMASK_X x
#define CCMASK_Y y
#define CCMASK_W w
#define CCMASK_A 0.4375f
#define CCMASK_B 1.5f
#include "ccmask-body.h"
