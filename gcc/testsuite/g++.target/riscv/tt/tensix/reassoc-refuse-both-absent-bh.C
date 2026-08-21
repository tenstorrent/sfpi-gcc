// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fdump-tree-rvtt_combine" }
// Both-absent control (the corpus byte-gate shape): no license flag at
// all -- no "reassoc:" line anywhere, serial chain intact.
// { dg-final { scan-tree-dump-not "reassoc:" "rvtt_combine" } }
// { dg-final { scan-tree-dump "sfpadd \\(s2_" "rvtt_combine" } }
#define RA_KERNEL ra_both_absent
#define RA_N 4
#define RA_X0 x0
#define RA_X1 x1
#define RA_X2 x2
#define RA_X3 x3
#define RA_S1 s1
#define RA_S2 s2
#define RA_SL s3
#include "reassoc-body.h"
