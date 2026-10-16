// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fdump-tree-rvtt_combine" }
// Check that without -mtt-tensix-optimize-reassoc the bitwise integer chain is not rebalanced.
// { dg-final { scan-tree-dump-not "reassoc:" "rvtt_combine" } }
// { dg-final { scan-tree-dump "sfpxor \\(h2_" "rvtt_combine" } }
#define RA_KERNEL ra_xor_default_off
#define RA_N 4
#define RA_OP(a, b) __builtin_rvtt_sfpxor ((a), (b))
#define RA_X0 k0
#define RA_X1 k1
#define RA_X2 k2
#define RA_X3 k3
#define RA_S1 h1
#define RA_S2 h2
#define RA_SL h3
#include "reassoc-body.h"
