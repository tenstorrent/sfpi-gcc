// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fassociative-math -fno-signed-zeros -fno-trapping-math -mtt-tensix-optimize-reassoc -fdump-tree-rvtt_reassoc" }
// Renamed + varied twin (charter genericity): different names, a
// 6-term chain (depth 5 -> 3), and a second kernel chaining sfpmul
// instead of sfpadd (a product chain is the same licensed rebalance).
// The decision must not key on names, term count, or which plain-mod
// MAD-unit operator forms the chain.
// { dg-final { scan-tree-dump-times "reassoc: licensed rebalance depth 5->3 .sfpadd chain of 6 terms" 1 "rvtt_reassoc" } }
// { dg-final { scan-tree-dump-times "reassoc: licensed rebalance depth 3->2 .sfpmul chain of 4 terms" 1 "rvtt_reassoc" } }
// { dg-final { scan-tree-dump-not "refusing" "rvtt_reassoc" } }
#define RA_KERNEL ra_wide_acc
#define RA_N 6
#define RA_X0 alpha
#define RA_X1 beta
#define RA_X2 gamma
#define RA_X3 delta
#define RA_X4 eps
#define RA_X5 zeta
#define RA_S1 p1
#define RA_S2 p2
#define RA_S3 p3
#define RA_S4 p4
#define RA_SL pl
#include "reassoc-body.h"

#undef RA_KERNEL
#undef RA_N
#undef RA_OP
#undef RA_X0
#undef RA_X1
#undef RA_X2
#undef RA_X3
#undef RA_S1
#undef RA_S2
#undef RA_SL
#define RA_KERNEL ra_prod_chain
#define RA_N 4
#define RA_OP(a, b) __builtin_rvtt_sfpmul ((a), (b), 0)
#define RA_X0 m0
#define RA_X1 m1
#define RA_X2 m2
#define RA_X3 m3
#define RA_S1 q1
#define RA_S2 q2
#define RA_SL q3
#include "reassoc-body.h"
