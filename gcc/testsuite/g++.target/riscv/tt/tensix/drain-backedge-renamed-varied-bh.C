// Genericity twin (charter rule 3): renamed function, different Dst
// addresses -- the loop-backedge elision derives from dataflow, typed
// effects, and the descriptor's own SequenceBits, never names or
// constants, so the identical proof fires.  (A varied stride or
// predicate sense changes the WHOLE-WORD program proof and refuses at
// descriptor synthesis, before this mechanism -- the established
// compact-program envelope.)
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -mtt-tensix-optimize-drain-schedule -fdump-rtl-rvtt_macro_planner" }
// { dg-final { scan-rtl-dump "Macro-planner drain-schedule: loop-backedge drain elided" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "exit compensation 3 SFPNOPs" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-times "SFPNOP" 3 } }

#define SELECT_COND_ADDR 128
#define SELECT_TRUE_ADDR 192
#define SELECT_FALSE_ADDR 384
#define SELECT_STRIDE 2
#define SELECT_ADDR_MODE 7
#define DRAIN_LOOP_NAME renamed_predicated_merge_faces
#include "drain-backedge-select-loop-body.h"
