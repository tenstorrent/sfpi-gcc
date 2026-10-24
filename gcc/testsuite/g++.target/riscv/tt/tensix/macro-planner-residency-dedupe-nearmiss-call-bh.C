#include "rtl-lane-reset.h"

// Check that a call between two identical regions refuses de-duplication (resid-span-unproven).
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -mtt-tensix-macro-planner-residency -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump "Macro-planner residency-refusal: resid-span-unproven .prefix-epoch-unproven" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "resident=elided" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-times "SFPCONFIG" 12 } }

#include "macro-planner-tile-hoist-body.h"

/* A known, noninterposable owner changes the descriptor epoch without
   introducing an unproved recorder or incoming lane ABI.  */
static __attribute__((noinline)) void foreign_config_owner ()
{
  asm volatile (".ttinsn %0" :: "n" (0x91000001));
}

__attribute__((noinline)) void
call_split_dedupe_pair (unsigned faces_a, unsigned faces_b)
{
  RVTT_TEST_LANE_RESET ();
  unsigned face = 0;
  do
    {
      TILE_ROW (); TILE_ROW (); TILE_ROW (); TILE_ROW ();
      TILE_ROW (); TILE_ROW (); TILE_ROW (); TILE_ROW ();
    }
  while (++face < faces_a);
  foreign_config_owner ();
  RVTT_TEST_LANE_RESET ();
  unsigned face2 = 0;
  do
    {
      TILE_ROW (); TILE_ROW (); TILE_ROW (); TILE_ROW ();
      TILE_ROW (); TILE_ROW (); TILE_ROW (); TILE_ROW ();
    }
  while (++face2 < faces_b);
}
