// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-unroll-loops -mno-tt-tensix-optimize-replay -mtt-tensix-optimize-lreg-rename -mtt-tensix-optimize-lreg-rename-chains -fdump-rtl-rvtt_lreg_rename_chains-details" }
// Flag composition after the W4-C v1 retirement: BOTH flags on runs
// the ONE general engine exactly once (the retired flag is an alias;
// there is no second pass to collide with).  Phase 1 renames the
// v1-profile latency-0 chain the retired pass used to take, phase 2
// the kill+read chain -- the same two renames the pre-retirement
// composed mode committed, now from a single engine.
// { dg-final { scan-rtl-dump "Lreg chain rename: renames=2" "rvtt_lreg_rename_chains" } }
// { dg-final { scan-rtl-dump-times "Lreg chain rename: renames=" 1 "rvtt_lreg_rename_chains" } }
// { dg-final { scan-rtl-dump-not "regrename-postcommit-divergence" "rvtt_lreg_rename_chains" } }
#define REN_FN renc_compose
#define REN_TRIPS 20
#define REN_K1 k1
#define REN_K2 k2
#define REN_X x
#define REN_T t
#define REN_P p
#define REN_Q q
#define REN_R r
#define REN_U u
#define REN_ROW row
#define REN_CONSUME(a, b) __builtin_rvtt_sfpxor ((a), (b))
#include "lreg-rename-body.h"
