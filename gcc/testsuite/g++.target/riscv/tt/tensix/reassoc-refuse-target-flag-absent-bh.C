// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fassociative-math -fno-signed-zeros -fno-trapping-math -fdump-tree-rvtt_combine" }
// THE CRITICAL REFUSAL, other half: the -fassociative-math license alone is NOT
// the license either.  Without -mtt-tensix-optimize-reassoc the
// reassociation pass does not run at all: no "reassoc:" line anywhere,
// and the serial chain link (the root consuming s2) survives to the
// combiner byte-identically.
// { dg-final { scan-tree-dump-not "reassoc:" "rvtt_combine" } }
// { dg-final { scan-tree-dump "sfpadd \\(s2_" "rvtt_combine" } }
#define RA_KERNEL ra_target_absent
#define RA_N 4
#define RA_X0 x0
#define RA_X1 x1
#define RA_X2 x2
#define RA_X3 x3
#define RA_S1 s1
#define RA_S2 s2
#define RA_SL s3
#include "reassoc-body.h"
