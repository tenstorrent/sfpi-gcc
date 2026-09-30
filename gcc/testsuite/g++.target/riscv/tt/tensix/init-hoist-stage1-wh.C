// WH mirror of the stage-1 fire: the capability tables carry the WH
// program; the identical contract fires from the WH descriptor.
// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -mtt-tensix-optimize-init-hoist -fdump-rtl-rvtt_macro_planner" }
// { dg-final { scan-rtl-dump "Macro-planner init-hoist: stage=1" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "init-hoist=descriptor" "rvtt_macro_planner" } }

#include "init-hoist-caller-body.h"
