#include "rtl-lane-reset.h"

// Check that a raw L0 value live across the caller's loop entry refuses the descriptor hoist.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -mtt-tensix-optimize-init-hoist -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump "init-hoist: refused \\(drain-init-fixed-lreg-live\\)" "rvtt_macro_planner" } }

#define INIT_LOOP_TAIL() __builtin_rvtt_sfprawlreg_effect (0x01, 0)
#include "init-hoist-caller-body.h"
