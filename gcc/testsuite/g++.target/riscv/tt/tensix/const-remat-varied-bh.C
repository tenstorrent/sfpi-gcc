// Constant-varied twin: entirely different fp32 immediates and trip
// count; the mechanism must be value-independent.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-remat -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump "const-remat: rematerialized " "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump "const-remat: pressure resolved: \\d+ -> \\d+" "rvtt_prgm_const" } }

#define NAME remat_varied_constants
#define C(N) v##N
#define K(N) (0xbf910000u ^ (0x1c2e0 + 37 * N))
#define TRIPS 12
#include "const-remat-body.h"
