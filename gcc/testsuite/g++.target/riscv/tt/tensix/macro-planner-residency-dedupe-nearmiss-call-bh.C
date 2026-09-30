// Residency de-duplication, near miss: a CALL between the two
// identical regions.  Content matches and dominance holds, but the
// function-wide owned-state invariance walk cannot see through the
// call -- the de-duplication refuses (resid-span-unproven) and the
// second region programs its own descriptor words exactly as without
// the flag.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -mtt-tensix-macro-planner-residency -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump "Macro-planner residency-refusal: resid-span-unproven .prefix-epoch-unproven" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "resident=elided" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-times "SFPCONFIG" 10 } }

#include "macro-planner-tile-hoist-body.h"

extern void foreign_config_owner ();

__attribute__((noinline)) void
call_split_dedupe_pair (unsigned faces_a, unsigned faces_b)
{
  unsigned face = 0;
  do
    {
      TILE_ROW (); TILE_ROW (); TILE_ROW (); TILE_ROW ();
      TILE_ROW (); TILE_ROW (); TILE_ROW (); TILE_ROW ();
    }
  while (++face < faces_a);
  foreign_config_owner ();
  unsigned face2 = 0;
  do
    {
      TILE_ROW (); TILE_ROW (); TILE_ROW (); TILE_ROW ();
      TILE_ROW (); TILE_ROW (); TILE_ROW (); TILE_ROW ();
    }
  while (++face2 < faces_b);
}
