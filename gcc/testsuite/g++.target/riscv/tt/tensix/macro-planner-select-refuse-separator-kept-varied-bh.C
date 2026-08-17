// Varied-constant twin of the cc-separator-kept-silicon-unproven
// refusal (2026-08-17 Where silicon adjudication, evidence root
// ~/sfpi-uplift/where-adjudication-20260817): different Dst
// addresses, the OPPOSITE mixed-mode pairing (condition mode 6,
// payload/store mode 2 -- the reverse of
// macro-planner-select-form-bh.C), and the EQ0 predicate sense.  The
// compact candidate still refuses its descriptor by name (mixed modes
// cannot ride the launch-sourced store mod0) and the established
// calendar still keeps its typed separator, so the schedule refuses
// identically: the refusal keys on the KEPT separator alone -- no
// address, mode value, sense, or misc word participates.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump "Macro-planner schedule-candidate: cc-compact" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner descriptor-refusal: cc-template-unproved" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner schedule-refusal: cc-separator-kept-silicon-unproven" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "Macro-planner descriptor-cc:" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "Macro-planner formed" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-not "\\.ttinsn" } }
// { dg-final { scan-assembler-not "SFPCONFIG" } }
// { dg-final { scan-assembler-not "SFPLOADMACRO" } }
// { dg-final { scan-assembler-times "SFPSETCC" 1 } }
// { dg-final { scan-assembler-times "TTINCRWC" 8 } }

#define SELECT_COND_ADDR 16
#define SELECT_TRUE_ADDR 96
#define SELECT_FALSE_ADDR 192
#define SELECT_COND_MODE 6
#define SELECT_PAYLOAD_MODE 2
#define SELECT_SETCC_MOD 6
#define SELECT_ADDR_MODE 7
#include "macro-planner-select-body.h"

__attribute__((noinline)) void select_rows_varied_refusal ()
{
  SELECT_ROWS_8 ();
}
