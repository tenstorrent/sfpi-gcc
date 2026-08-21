// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-exceptions -fno-rtti -fassociative-math -fno-signed-zeros -fno-trapping-math -mtt-tensix-optimize-reassoc -fdump-tree-rvtt_reassoc" }
// Arch breadth: the licensed rebalance is target-independent gimple
// (charter: >= 2 unrelated shapes/targets).  WH load addr-mode 3.
// { dg-final { scan-tree-dump-times "reassoc: licensed rebalance depth 3->2 .sfpadd chain of 4 terms" 1 "rvtt_reassoc" } }
// { dg-final { scan-tree-dump-not "refusing" "rvtt_reassoc" } }
#define RA_ADDR_MODE 3
#define RA_KERNEL ra_fire_wh
#define RA_N 4
#define RA_X0 wx0
#define RA_X1 wx1
#define RA_X2 wx2
#define RA_X3 wx3
#define RA_S1 ws1
#define RA_S2 ws2
#define RA_SL ws3
#include "reassoc-body.h"
