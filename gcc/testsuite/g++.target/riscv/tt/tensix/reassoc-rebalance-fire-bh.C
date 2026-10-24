// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fassociative-math -fno-signed-zeros -fno-trapping-math -mtt-tensix-optimize-reassoc -fdump-tree-rvtt_reassoc -fdump-tree-rvtt_combine" }
// Check that a licensed 4-term sfpadd chain rebalances to a balanced tree (depth 3 -> 2).
// { dg-final { scan-tree-dump-times "reassoc: licensed rebalance depth 3->2 .sfpadd chain of 4 terms" 1 "rvtt_reassoc" } }
// { dg-final { scan-tree-dump-not "refusing" "rvtt_reassoc" } }
// { dg-final { scan-tree-dump-not "sfpadd \\(s2_" "rvtt_combine" } }
#define RA_KERNEL ra_fire_kernel
#define RA_N 4
#define RA_X0 x0
#define RA_X1 x1
#define RA_X2 x2
#define RA_X3 x3
#define RA_S1 s1
#define RA_S2 s2
#define RA_SL s3
#include "reassoc-body.h"
