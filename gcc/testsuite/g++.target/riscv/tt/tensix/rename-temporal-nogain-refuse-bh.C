// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-unroll-loops -mno-tt-tensix-optimize-replay -mtt-tensix-optimize-lreg-rename-chains -mtt-tensix-optimize-rename-temporal -fdump-rtl-rvtt_lreg_rename_chains-details" }
// Check that a temporal rename candidate with no modeled issue-slot gain refuses (regrename-temporal-no-modeled-gain).
// { dg-final { scan-rtl-dump "regrename-temporal-no-modeled-gain" "rvtt_lreg_rename_chains" } }
// { dg-final { scan-rtl-dump-not "Lreg chain rename \\(temporal\\)" "rvtt_lreg_rename_chains" } }
// { dg-final { scan-rtl-dump-not "regrename-postcommit-divergence" "rvtt_lreg_rename_chains" } }
#define RENT_FN rent_nogain
#include "rename-temporal-body.h"
