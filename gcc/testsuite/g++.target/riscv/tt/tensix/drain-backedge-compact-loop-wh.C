// WH mirror of drain-backedge-compact-loop-bh.C: the capability tables
// carry the WH compact program; the identical backedge proof fires
// from the WH descriptor's own SequenceBits.
// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -mtt-tensix-optimize-drain-schedule -fdump-rtl-rvtt_macro_planner" }
// { dg-final { scan-rtl-dump "Macro-planner drain-schedule: loop-backedge drain elided" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "exit compensation 3 SFPNOPs" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-times "SFPNOP" 3 } }

#define SELECT_ADDR_MODE 3
#include "drain-backedge-select-loop-body.h"
