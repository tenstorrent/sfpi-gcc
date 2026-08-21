// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-reassoc -fdump-tree-rvtt_reassoc -fdump-tree-rvtt_combine" }
// THE CRITICAL REFUSAL: -mtt-tensix-optimize-reassoc alone is NOT the
// license.  Without the -fassociative-math license the FP chain refuses BY NAME
// (associative-math-license-absent), nothing is rebalanced, and the
// serial link (the root consuming s2) survives to the combiner
// byte-identically.
// { dg-final { scan-tree-dump-times "associative-math-license-absent" 1 "rvtt_reassoc" } }
// { dg-final { scan-tree-dump-not "licensed rebalance" "rvtt_reassoc" } }
// { dg-final { scan-tree-dump-not "integer rebalance" "rvtt_reassoc" } }
// { dg-final { scan-tree-dump "sfpadd \\(s2_" "rvtt_combine" } }
#define RA_KERNEL ra_license_absent
#define RA_N 4
#define RA_X0 x0
#define RA_X1 x1
#define RA_X2 x2
#define RA_X3 x3
#define RA_S1 s1
#define RA_S2 s2
#define RA_SL s3
#include "reassoc-body.h"
