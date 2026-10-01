// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-unroll-loops -mno-tt-tensix-optimize-replay -mtt-tensix-optimize-lreg-rename -mtt-tensix-optimize-interlock-schedule -fdump-rtl-rvtt_lreg_rename_chains-details -fdump-rtl-rvtt_schedule-details" }
// The retired single-shape pass's fire twin, retargeted at the retirement: the
// -mtt-tensix-optimize-lreg-rename flag is frozen API and now requests
// the GENERAL du-chain engine (the single-shape v1 pass was retired as
// wrong-code-bearing; see rtl-rvtt-lreg-rename.cc).  On v1's own fire
// body the general engine renames the v1-profile chain (phase 1) AND
// the dest-reuses-dying-source chain v1 could not see, and the
// interlock fill (which the storage collision blocked -- see
// lreg-rename-off-control-bh.C) then moves the second materialization
// into the audited mul->mul stall.
// { dg-final { scan-rtl-dump-times "Lreg chain rename: L\\d+ -> L\\d+" 2 "rvtt_lreg_rename_chains" } }
// { dg-final { scan-rtl-dump-not "regrename-postcommit-divergence" "rvtt_lreg_rename_chains" } }
// { dg-final { scan-rtl-dump "Interlock-fill moved uid=\\d+ into the stall" "rvtt_schedule" } }
#define REN_FN ren_fire
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
