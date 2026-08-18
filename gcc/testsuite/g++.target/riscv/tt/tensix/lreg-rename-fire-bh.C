// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-unroll-loops -mno-tt-tensix-optimize-replay -mtt-tensix-optimize-lreg-rename -mtt-tensix-optimize-interlock-schedule -fdump-rtl-rvtt_lreg_rename-details -fdump-rtl-rvtt_schedule-details" }
// Two independent invariant-input materializations packed into one
// LREG by first-fit reuse: the rename moves the first chain to a
// proven-free register, and the interlock fill (which the storage
// collision blocked -- see lreg-rename-off-control-bh.C) then moves
// the second materialization into the audited mul->mul stall.
// { dg-final { scan-rtl-dump-times "Lreg rename: chain L\\d+ -> L\\d+" 1 "rvtt_lreg_rename" } }
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
