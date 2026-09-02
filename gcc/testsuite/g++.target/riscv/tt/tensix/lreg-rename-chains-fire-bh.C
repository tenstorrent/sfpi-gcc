// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-unroll-loops -mno-tt-tensix-optimize-replay -mtt-tensix-optimize-lreg-rename-chains -fdump-rtl-rvtt_lreg_rename_chains-details" }
// The the du-chain rename engine fire twin: a two-chain rename in a self-loop row the
// retired v1 single-shape pass refused outright (every colliding
// writer carries a nonzero audited latency -- the multi-member
// class).  The general engine renames both storage-collision chains
// under the whole-row no-worse acceptance.
// { dg-final { scan-rtl-dump-times "Lreg chain rename: L\\d+ -> L\\d+" 2 "rvtt_lreg_rename_chains" } }
// { dg-final { scan-rtl-dump "Lreg chain rename: renames=2" "rvtt_lreg_rename_chains" } }
// { dg-final { scan-rtl-dump-not "regrename-postcommit-divergence" "rvtt_lreg_rename_chains" } }
#define RENC_FN renc_fire
#define RENC_TRIPS 20
#define RENC_K1 k1
#define RENC_K2 k2
#define RENC_X x
#define RENC_T t
#define RENC_P p
#define RENC_R r
#define RENC_U u
#define RENC_T2 t2
#define RENC_R2 r2
#define RENC_U2 u2
#define RENC_R3 r3
#define RENC_ROW row
#define RENC_CONSUME(a, b) __builtin_rvtt_sfpxor ((a), (b))
#include "lreg-rename-chains-body.h"
