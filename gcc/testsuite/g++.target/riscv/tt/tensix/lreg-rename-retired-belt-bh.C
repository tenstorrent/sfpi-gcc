// { dg-options "-mcpu=tt-bh-tensix -O2 -fchecking=2 -fno-exceptions -fno-rtti -fno-unroll-loops -mno-tt-tensix-optimize-replay -mtt-tensix-optimize-lreg-rename -fdump-rtl-rvtt_lreg_rename_chains-details" }
// Retirement witness (the source-read class, armed): the retired
// single-shape pass's whole-pattern writer edit rewrote genuine input
// reads of the destination register's previous live range (the
// dest-reuses-dying-source shape -- the calculate_lcm/cube_root/sine
// wrong-code fires adjudicated on their committed streams).  The
// general engine's writer edit is dest-only and its post-commit belt
// re-proves the operand webs on the committed stream; under
// -fchecking=2 any writer-source rewrite is a hard assert.  This body
// fires the kill+read chains (the close genuinely reads the chain
// value through clean OP_IN operands) with the belt armed: the
// compilation is clean and every fire re-verifies.
// { dg-final { scan-rtl-dump-times "Lreg chain rename: L\\d+ -> L\\d+" 4 "rvtt_lreg_rename_chains" } }
// { dg-final { scan-rtl-dump-times "close=kill\\+read" 3 "rvtt_lreg_rename_chains" } }
// { dg-final { scan-rtl-dump-not "regrename-postcommit-divergence" "rvtt_lreg_rename_chains" } }
#define REN_FN belt_row
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
