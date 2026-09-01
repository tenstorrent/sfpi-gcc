// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-unroll-loops -mno-tt-tensix-optimize-replay -mtt-tensix-optimize-lreg-rename-chains -mtt-tensix-optimize-rename-temporal -fdump-rtl-rvtt_lreg_rename_chains-details" }
// R1 temporal-target FIRE: every architectural LREG is touched in the
// row (whole-block-free target selection is exhausted -- the 8-LREG
// wall), but one register's lifetimes are TEMPORALLY disjoint from the
// colliding chain's span: its only later touch opens with a fresh
// all-write definition and no CC event sits between the chain close
// and it.  The temporal tier admits it; the sibling control twin pins
// the same TU refusing regrename-no-free-lreg without the flag.
// { dg-final { scan-rtl-dump "Lreg chain rename \\(temporal\\): L\\d+ -> L\\d+" "rvtt_lreg_rename_chains" } }
// { dg-final { scan-rtl-dump-not "regrename-postcommit-divergence" "rvtt_lreg_rename_chains" } }
#define RENT_FN rent_fire
#include "rename-temporal-body.h"
