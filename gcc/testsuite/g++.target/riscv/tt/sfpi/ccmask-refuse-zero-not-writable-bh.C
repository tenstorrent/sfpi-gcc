// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -Wno-deprecated-declarations -mtt-tensix-optimize-ccmask -fdump-tree-rvtt_ccmask" }
// Check that a strict-direction zeroing of vConst0 is refused (ccmask-zero-not-writable).
// { dg-final { scan-tree-dump "ccmask refused .ccmask-zero-not-writable" "rvtt_ccmask" } }
// { dg-final { scan-tree-dump "ccmask: folds=0" "rvtt_ccmask" } }
// { dg-final { scan-assembler-not "SFPLE" } }
// { dg-final { scan-assembler "SFPGT\tL9, L\[0-7\], 0, 1" } }
// { dg-final { scan-assembler "SFPENCC" } }
#define CCMASK_FN ccmask_ltdir_cregzero
#define CCMASK_COND(x) ((x) < 0.0f)
#define CCMASK_ZERO sfpi::impl_::LRegFile::vCReg<sfpi::vFloat>(sfpi::CREG_IDX_0)
#define CCMASK_X x
#define CCMASK_Y y
#define CCMASK_W w
#define CCMASK_A 0.4375f
#define CCMASK_B 1.5f
#include "ccmask-body.h"
