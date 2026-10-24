#include "rtl-lane-reset.h"

// Check that a second face-loop region with an identical descriptor program elides its descriptor words.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -mtt-tensix-macro-planner-residency -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump "Macro-planner residency: descriptor program content-identical to a dominating resident program; 5 descriptor words elided" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner formed: rows=8 runs=1 config=preheader lane-proof=materialized-enable resident=elided" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-times "SFPCONFIG" 6 } }
// { dg-final { scan-assembler-times "SFPENCC" 3 } }

#include "macro-planner-tile-hoist-body.h"

__attribute__((noinline)) void
dedupe_two_face_loops (unsigned faces_a, unsigned faces_b)
{
  RVTT_TEST_LANE_RESET ();
  unsigned face = 0;
  do
    {
      TILE_ROW (); TILE_ROW (); TILE_ROW (); TILE_ROW ();
      TILE_ROW (); TILE_ROW (); TILE_ROW (); TILE_ROW ();
    }
  while (++face < faces_a);
  unsigned face2 = 0;
  do
    {
      TILE_ROW (); TILE_ROW (); TILE_ROW (); TILE_ROW ();
      TILE_ROW (); TILE_ROW (); TILE_ROW (); TILE_ROW ();
    }
  while (++face2 < faces_b);
}
