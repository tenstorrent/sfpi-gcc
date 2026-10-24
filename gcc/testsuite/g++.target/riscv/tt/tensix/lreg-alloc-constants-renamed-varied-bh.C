// Check that the nine-constant pressure body compiles under the LREG allocator with renamed and varied constants.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-lreg-alloc -mtt-tensix-dst-layout-32b -fdump-rtl-rvtt_lp_alloc-details" }
// { dg-final { scan-rtl-dump "colorability=proven" "rvtt_lp_alloc" } }
// { dg-final { scan-assembler {\mSFPSTORE\tL[0-7], 252, 4, 7} } }

#define NAME lreg_alloc_varied
#define C(N) q##N
#define K(N) (0x3f1200a0u + 0x1111u * N)
#define TRIPS 17
#define CONST_RESIDENCY_ENTRY_PROLOGUE
#include "const-remat-body.h"
