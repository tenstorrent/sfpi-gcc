// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fassociative-math -fno-signed-zeros -fno-trapping-math -mtt-tensix-optimize-reassoc -mtt-tensix-optimize-cc-region-general -fdump-tree-rvtt_reassoc" }
// Check that a CC writer at the chain window's own frame depth refuses the rebalance even under -mtt-tensix-optimize-cc-region-general.
// { dg-final { scan-tree-dump-times "reassoc-cc-region-boundary" 1 "rvtt_reassoc" } }
// { dg-final { scan-tree-dump-not "licensed rebalance" "rvtt_reassoc" } }
#define RA_KERNEL ra_ambient_cc_general
#define RA_N 4
#define RA_MID() __builtin_rvtt_sfpencc (10, 0)
#define RA_X0 x0
#define RA_X1 x1
#define RA_X2 x2
#define RA_X3 x3
#define RA_S1 s1
#define RA_S2 s2
#define RA_SL s3
#include "reassoc-body.h"
