// Check that raw L0 ownership live at the loop entry refuses descriptor staging.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -mtt-tensix-optimize-init-hoist -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump "init-hoist: refused \\(drain-init-fixed-lreg-live\\)" "rvtt_macro_planner" } }

#define INIT_CALLER_PRELUDE() __builtin_rvtt_sfprawlreg_access (0, 0x01)
#include "init-hoist-caller-body.h"
