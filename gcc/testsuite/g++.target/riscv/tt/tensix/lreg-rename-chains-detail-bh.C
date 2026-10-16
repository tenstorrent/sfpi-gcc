// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-unroll-loops -mno-tt-tensix-optimize-replay -mtt-tensix-optimize-lreg-rename-chains -fdump-rtl-rvtt_lreg_rename_chains-details" }
// Check that the du-chain rename renames both a kill-close chain and a kill+read chain, moving only register fields.
// { dg-final { scan-rtl-dump "Lreg chain rename: L\\d+ -> L\\d+ in bb \\d+ \\(def uid=\\d+, 1 readers, close=kill\\)" "rvtt_lreg_rename_chains" } }
// { dg-final { scan-rtl-dump "Lreg chain rename: L\\d+ -> L\\d+ in bb \\d+ \\(def uid=\\d+, 0 readers, close=kill\\+read\\)" "rvtt_lreg_rename_chains" } }
// { dg-final { scan-rtl-dump-times "words before == \\d+ words after, register fields only" 2 "rvtt_lreg_rename_chains" } }
// { dg-final { scan-rtl-dump "Lreg chain rename: renames=2" "rvtt_lreg_rename_chains" } }
// { dg-final { scan-rtl-dump-not "regrename-postcommit-divergence" "rvtt_lreg_rename_chains" } }
#define REN_FN renc_chains
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
