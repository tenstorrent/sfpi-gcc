// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-ccmask -fdump-tree-rvtt_ccmask" }
// Renamed-equivalent, different constants: the fold decision must be
// value- and name-independent (dataflow shape + architectural facts
// only).
// { dg-final { scan-tree-dump-times "ccmask: folded zeroing CC region" 1 "rvtt_ccmask" } }
// { dg-final { scan-assembler-times "SFPGT\tL\[0-7\], L9, 0, 8" 1 } }
// { dg-final { scan-assembler-not "SFPSETCC" } }
#define CCMASK_FN totally_different_name
#define CCMASK_X aardvark
#define CCMASK_Y bilby
#define CCMASK_W caracal
#define CCMASK_A (-2.625f)
#define CCMASK_B 0.0078125f
#include "ccmask-body.h"
