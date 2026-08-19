// WP15 (upward-IMS carrier former, the upward half of the WP14 mapping):
// the row carries a fresh shift/product pair reading the RAW second
// operand ahead of its in-place cooking chain.  The established search
// (with WP14 repair) proves ii=13 with the pair explicit: the pair's
// results cannot ride the two value carriers (their registers are no
// carrier's destination) and the template budget is not the obstacle --
// placement is.  Under -mtt-tensix-macro-ims-carrier the former re-loads
// the second operand's row address into a provably free launch-encodable
// register (L3; VDLo encodes 0..3 only), version-split-renames the
// two-member chain (the fresh shift, then its in-place product
// continuation) onto the new carrier, and commits only after the mutated
// region re-proves through the FULL established pipeline at ii=12 --
// both hosted chain events SHARE the existing derived template words
// bit-identically (the SHFT2(-23) pair word and the VA=L0 MUL24 word),
// so the four-template budget holds.  Every obligation that fails
// reverts the mutation byte-identically under a stable name.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -mtt-tensix-macro-ims -mtt-tensix-macro-ims-carrier -fdump-rtl-rvtt_macro_planner" }
// { dg-final { scan-rtl-dump-times "Macro-planner upward-carrier: seed=2 chain=.3,4. reload-vd=3 prefix-clones=0 ii=13->12" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner upward-carrier: formed .ii=12, was 13." 1 "rvtt_macro_planner" } }
// The committed calendar: four launches, four templates; the moved
// shift shares the SHFT2(-23) word and the moved product shares the
// VA=L0 MUL24 word bit-identically:
// { dg-final { scan-rtl-dump "Macro-planner descriptor: templates=4 seq=4 misc=0x00000080 setc16=3 launches=4 drain=2" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner descriptor-word dest=1: 0x94fe90d6" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner descriptor-word dest=2: 0x980009e0" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner descriptor-launch: macro=0 vd=3" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner formed: rows=8 runs=1" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner verify: ok" "rvtt_macro_planner" } }
// Four launch words per row survive as planner words (8 rows each); the
// replay unit compresses the explicit spine.
// { dg-final { scan-assembler-times "\\.ttinsn\\t2469716032" 8 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2470764544" 8 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2476007488" 8 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2481242112" 8 } }
// { dg-final { scan-assembler-times "TTREPLAY\\t0, 8, 1, 1" 1 } }
// { dg-final { scan-assembler-not "TTINCRWC" } }

#include "macro-planner-ims-carrier-row.h"

__attribute__((noinline)) void paired_shift_product_rows ()
{
  ROW (-23, 64); ROW (-23, 64); ROW (-23, 64); ROW (-23, 64);
  ROW (-23, 64); ROW (-23, 64); ROW (-23, 64); ROW (-23, 64);
}
