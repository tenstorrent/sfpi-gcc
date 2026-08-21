// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-exceptions -fno-rtti -fassociative-math -fno-signed-zeros -fno-trapping-math -mtt-tensix-optimize-reassoc -fdump-tree-rvtt_combine" }
// Renamed twin on the OTHER target (charter genericity + arch
// breadth): the licensed multi-use fusion must be identical under
// renaming and on WH.
// { dg-final { scan-tree-dump-times "reassoc: licensed mad-fuse of multi-use mul" 1 "rvtt_combine" } }
// { dg-final { scan-assembler-times "SFPMAD\t" 1 } }
// { dg-final { scan-assembler-times "SFPMUL\t" 1 } }
// { dg-final { scan-assembler-not "SFPADD\t" } }
#define RMF_ADDR_MODE 3
#define RMF_KERNEL wmf_fire
#define RMF_A wa
#define RMF_B wb
#define RMF_C wc
#define RMF_P wp
#define RMF_R wr
#include "reassoc-madfuse-body.h"
