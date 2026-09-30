// Wormhole twin of the fire test: the mechanism is not BH-specific.
// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-remat -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump "const-remat: rematerialized " "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump "const-remat: pressure resolved: \\d+ -> \\d+" "rvtt_prgm_const" } }

#define NAME remat_pressure_fire_wh
#define C(N) c##N
#define K(N) (0x3e4b0000u + N)
#define TRIPS 32
#include "const-remat-body.h"
