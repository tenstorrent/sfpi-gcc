// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-unroll-loops -mno-tt-tensix-optimize-replay -mtt-tensix-optimize-lreg-rename-chains -fdump-rtl-rvtt_lreg_rename_chains-details" }
// Control for the temporal fire twin: the IDENTICAL TU without
// -mtt-tensix-optimize-rename-temporal.  The whole-block-free target
// search is exhausted (every LREG touched or live), so the chain
// refuses regrename-no-free-lreg and no temporal target is ever
// admitted -- the tier's gate is the flag, not the shape.
// { dg-final { scan-rtl-dump "Lreg chain rename refused: regrename-no-free-lreg" "rvtt_lreg_rename_chains" } }
// { dg-final { scan-rtl-dump-not "\\(temporal\\)" "rvtt_lreg_rename_chains" } }
#define RENT_FN rent_control
#include "rename-temporal-body.h"
