// Renamed/varied generality twin: the shared nine-invariant-constant
// pressure body (different function name, constants and trip count)
// compiles under the DSATUR allocator with no relief flags.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-lreg-alloc -mtt-tensix-dst-layout-32b -fdump-rtl-rvtt_lp_alloc-details" }
// { dg-final { scan-rtl-dump "colorability=proven" "rvtt_lp_alloc" } }
// { dg-final { scan-assembler {\mSFPSTORE\tL[0-7], 252, 4, 7} } }

#define NAME lreg_alloc_varied
#define C(N) q##N
#define K(N) (0x3f1200a0u + 0x1111u * N)
#define TRIPS 17
#include "const-remat-body.h"
