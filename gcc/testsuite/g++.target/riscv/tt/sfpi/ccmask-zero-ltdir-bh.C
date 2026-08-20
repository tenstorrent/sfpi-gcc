// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-ccmask -fdump-tree-rvtt_ccmask" }
// The strict-less direction: zeroing under `x < 0.0f` folds to the
// swapped-operand SFPLE keep-mask (0 <= x on the sign-magnitude total
// order = sign clear = the SETCC mod0 complement), with the region's
// own writable loadi-zero as the SET_DEST (written) operand -- never
// the read-only constant register L9.  Proof artifact:
// tt/proofs/ccmask-direction-complete/ (EQUAL over 2^32).
// { dg-final { scan-tree-dump-times "ccmask: folded zeroing CC region" 1 "rvtt_ccmask" } }
// { dg-final { scan-assembler-times "SFPLE\tL\[0-7\], L\[0-7\], 0, 8" 1 } }
// { dg-final { scan-assembler-not "SFPLE\tL\[0-7\], L9" } }
// { dg-final { scan-assembler "SFPAND" } }
// { dg-final { scan-assembler-not "SFPSETCC" } }
// { dg-final { scan-assembler-not "SFPCOMPC" } }
// { dg-final { scan-assembler-not "SFPENCC" } }
#define CCMASK_FN ccmask_ltdir_fire
#define CCMASK_COND(x) ((x) < 0.0f)
#define CCMASK_X x
#define CCMASK_Y y
#define CCMASK_W w
#define CCMASK_A 0.4375f
#define CCMASK_B 1.5f
#include "ccmask-body.h"
