// Loop-backedge drain elision (lane CA, the drain-route remainder) on
// the TTNN-Where-class compact loop: the loop-body region's final run
// ends at the latch, so its derived drain used to execute once per
// trip.  The backedge follower stream proves -- the loop tail is
// scalar-only (the typed separators are absorbed), and the region's
// own first-run accesses clear the DECODED pending horizon (the
// SequenceBits pend 1 slot; the emitted drain 3 is the conservative
// proven-calendar figure) -- so the in-body drain is elided and the
// full derived drain lands once on the loop's exit path.  The exit
// contract is preserved: SFPNOP 3 per function, after the loop.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -mtt-tensix-optimize-drain-schedule -fdump-rtl-rvtt_macro_planner" }
// { dg-final { scan-rtl-dump "Macro-planner drain-backedge: drain=3 pending=1 stream-credit=0 words-per-row=3" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner drain-schedule: loop-backedge drain elided \\(drain=1 stream-credit=0\\)" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "exit compensation 3 SFPNOPs" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner formed: rows=8 runs=1 config=preheader lane-proof=materialized-enable drain-backedge" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-times "SFPNOP" 3 } }
// { dg-final { scan-assembler-not "SFPSETCC" } }
// { dg-final { scan-assembler-not "TTINCRWC" } }

#define SELECT_ADDR_MODE 7
#include "drain-backedge-select-loop-body.h"
