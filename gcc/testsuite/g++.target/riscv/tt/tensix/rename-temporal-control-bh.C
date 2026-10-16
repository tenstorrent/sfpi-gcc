// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-unroll-loops -mno-tt-tensix-optimize-replay -mtt-tensix-optimize-lreg-rename-chains -fdump-rtl-rvtt_lreg_rename_chains-details" }
// Check that without -mtt-tensix-optimize-rename-temporal an exhausted LREG file refuses with regrename-no-free-lreg and no temporal target is used.
// { dg-final { scan-rtl-dump "Lreg chain rename refused: regrename-no-free-lreg" "rvtt_lreg_rename_chains" } }
// { dg-final { scan-rtl-dump-not "\\(temporal\\)" "rvtt_lreg_rename_chains" } }
#define RENT_FN rent_control
#include "rename-temporal-body.h"
