// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-unroll-loops -mno-tt-tensix-optimize-replay -mtt-tensix-optimize-interlock-schedule -fdump-rtl-rvtt_schedule-details" }
// Default-off control: without the rename the storage collision stands
// and the interlock fill cannot move the second materialization into
// the stall.
// { dg-final { scan-rtl-dump-not "Interlock-fill moved" "rvtt_schedule" } }
// { dg-final { scan-assembler-not "rvtt_lreg_rename" } }
#define REN_FN ren_ctl
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
