// { dg-options "-mcpu=tt-bh-tensix -O2 -fchecking=2 -fno-exceptions -fno-rtti -fno-unroll-loops -mno-tt-tensix-optimize-replay -mtt-tensix-optimize-lreg-rename-chains -fdump-rtl-rvtt_lreg_rename_chains-details" }
// Check that kill+read chains rename without rewriting writer source operands, verified under -fchecking=2.
// { dg-final { scan-rtl-dump-times "Lreg chain rename: L\\d+ -> L\\d+" 4 "rvtt_lreg_rename_chains" } }
// { dg-final { scan-rtl-dump-times "close=kill\\+read" 3 "rvtt_lreg_rename_chains" } }
// { dg-final { scan-rtl-dump-not "regrename-postcommit-divergence" "rvtt_lreg_rename_chains" } }
#define REN_FN source_read_row
#define REN_TRIPS 16
#define REN_K1 b_k1
#define REN_K2 b_k2
#define REN_X b_x
#define REN_T b_t
#define REN_P b_p
#define REN_Q b_q
#define REN_R b_r
#define REN_U b_u
#define REN_ROW b_row
#define REN_CONSUME(a, b) __builtin_rvtt_sfpor ((a), (b))
#include "lreg-rename-body.h"
