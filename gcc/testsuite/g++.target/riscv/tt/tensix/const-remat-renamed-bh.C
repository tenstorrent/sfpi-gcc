// Renamed-equivalent twin of const-remat-fire-bh.C: different function
// and value names, same shape; the mechanism must be name-independent.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-remat -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump "const-remat: rematerialized " "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump "const-remat: pressure resolved: \\d+ -> \\d+" "rvtt_prgm_const" } }

#define NAME harbor_lantern_sweep
#define C(N) tide_##N
#define K(N) (0x3e4b0000u + N)
#define TRIPS 32
#include "const-remat-body.h"
