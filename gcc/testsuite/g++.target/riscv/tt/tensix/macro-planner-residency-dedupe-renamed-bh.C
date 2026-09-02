// Residency de-duplication, renamed-equivalent twin: identical
// two-region shape under fresh names.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -mtt-tensix-macro-planner-residency -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump "Macro-planner residency: descriptor program content-identical to a dominating resident program; 5 descriptor words elided" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner formed: rows=8 runs=1 config=preheader lane-proof=materialized-enable resident=elided" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-times "SFPCONFIG" 5 } }

#include "macro-planner-tile-hoist-body.h"

__attribute__((noinline)) void
renamed_dedupe_kernel_pair (unsigned first_span, unsigned second_span)
{
  unsigned outer_counter = 0;
  do
    {
      TILE_ROW (); TILE_ROW (); TILE_ROW (); TILE_ROW ();
      TILE_ROW (); TILE_ROW (); TILE_ROW (); TILE_ROW ();
    }
  while (++outer_counter < first_span);
  unsigned trailing_counter = 0;
  do
    {
      TILE_ROW (); TILE_ROW (); TILE_ROW (); TILE_ROW ();
      TILE_ROW (); TILE_ROW (); TILE_ROW (); TILE_ROW ();
    }
  while (++trailing_counter < second_span);
}
