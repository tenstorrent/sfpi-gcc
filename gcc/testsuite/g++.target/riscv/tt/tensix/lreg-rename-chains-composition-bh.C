// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-unroll-loops -mno-tt-tensix-optimize-replay -mtt-tensix-optimize-lreg-rename -mtt-tensix-optimize-lreg-rename-chains -fdump-rtl-rvtt_lreg_rename-details -fdump-rtl-rvtt_lreg_rename_chains-details" }
// Composition: with BOTH passes on, v1 fires first on its latency-0
// invariant-input chain, and the general engine still finds the
// kill+read chain in the SAME row afterwards -- the passes compose,
// they do not collide.
// { dg-final { scan-rtl-dump "Lreg rename: renames=1" "rvtt_lreg_rename" } }
// { dg-final { scan-rtl-dump "Lreg chain rename: renames=1" "rvtt_lreg_rename_chains" } }
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
