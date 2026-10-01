// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-unroll-loops -mno-tt-tensix-optimize-replay -mtt-tensix-optimize-lreg-rename-chains -mtt-tensix-optimize-rename-temporal -fdump-rtl-rvtt_lreg_rename_chains-details" }
// R1 temporal-tier PRICING twin (this TU was the tier's fire twin
// before the round-6 device re-pricing): every architectural LREG is
// touched in the row, so whole-block-free target selection is
// exhausted, and one register's lifetimes are temporally disjoint from
// the colliding chain's span -- the temporal ADMISSION shape is
// present.  Under the strict-gain acceptance the candidate must now
// also BUY modeled issue slots, and no temporal rename can: the priced
// worlds are isomorphic through the chain close, and the coupling at
// the target's fresh definition can only add slots.  The candidate
// refuses by name and no temporal rename ever commits; the device
// evidence behind the bar is the round-6 refusal pair (a hot row's
// interlock fills fell 48 to 16, +7.1% kernel cycles; one rename
// dissolved 8 replay launches, +4.2%).
// { dg-final { scan-rtl-dump "regrename-temporal-no-modeled-gain" "rvtt_lreg_rename_chains" } }
// { dg-final { scan-rtl-dump-not "Lreg chain rename \\(temporal\\)" "rvtt_lreg_rename_chains" } }
// { dg-final { scan-rtl-dump-not "regrename-postcommit-divergence" "rvtt_lreg_rename_chains" } }
#define RENT_FN rent_nogain
#include "rename-temporal-body.h"
