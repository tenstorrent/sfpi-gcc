// An externally enterable callee may have call sites outside the TU.
// The init-hoist service must not remove its per-call initialization
// based only on the one caller visible in the cgraph.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -mtt-tensix-optimize-init-hoist -fdump-rtl-rvtt_macro_planner" }
// { dg-final { scan-rtl-dump "init-hoist: closure \\(callee-external-entry\\)" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "init contract hoisted to caller" "rvtt_macro_planner" } }

#define INIT_CALLEE_LINKAGE
#include "init-hoist-caller-body.h"
