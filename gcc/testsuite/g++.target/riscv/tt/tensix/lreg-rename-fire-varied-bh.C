// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-unroll-loops -mno-tt-tensix-optimize-replay -mtt-tensix-optimize-lreg-rename-chains -fdump-rtl-rvtt_lreg_rename_chains-details" }
// Check that the du-chain rename is name-independent and renames two more chains through an OR consumer.
// { dg-final { scan-rtl-dump-times "Lreg chain rename: L\\d+ -> L\\d+" 4 "rvtt_lreg_rename_chains" } }
// { dg-final { scan-rtl-dump "Lreg chain rename: renames=4" "rvtt_lreg_rename_chains" } }
// { dg-final { scan-rtl-dump-not "regrename-postcommit-divergence" "rvtt_lreg_rename_chains" } }
#define REN_FN wallaby_row
#define REN_TRIPS 12
#define REN_K1 quokka
#define REN_K2 dingo
#define REN_X numbat
#define REN_T tuatara
#define REN_P pademelon
#define REN_Q quoll
#define REN_R rosella
#define REN_U uakari
#define REN_ROW lap
#define REN_CONSUME(a, b) __builtin_rvtt_sfpor ((a), (b))
#include "lreg-rename-body.h"
