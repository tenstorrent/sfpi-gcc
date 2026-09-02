// Residency de-duplication, varied twin: the second region's
// predicate uses the COMPLEMENTED source sense (setcc mod 6 = EQ0), so
// its derived template word differs -- the canonical CONTENT key does
// not match, no elision happens, and both regions program their own
// descriptor words exactly as without the flag.  Different payload
// addresses alone (launch words) do NOT defeat de-duplication -- the
// descriptor program is address-independent by derivation; the key is
// content, never shape.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -mtt-tensix-macro-planner-residency -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump-not "resident=elided" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "Macro-planner residency: descriptor program content-identical" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-times "SFPCONFIG" 10 } }

#include "macro-planner-tile-hoist-body.h"

#define TILE_ROW_EQ0()                                                        \
  do                                                                          \
    {                                                                         \
      auto condition = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);       \
      auto on_true = __builtin_rvtt_sfpload (nullptr, 32, 0, 0, 6, 7);        \
      auto on_false = __builtin_rvtt_sfpload (nullptr, 64, 0, 0, 6, 7);       \
      __builtin_rvtt_sfppushc (0);                                            \
      __builtin_rvtt_sfpsetcc_v (condition, 6);                               \
      auto result = __builtin_rvtt_sfpassign_lv (on_false, on_true);          \
      __builtin_rvtt_sfppopc (0);                                             \
      __builtin_rvtt_sfpstore (nullptr, result, 0, 0, 0, 6, 7);               \
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);                                   \
    }                                                                         \
  while (0)

__attribute__((noinline)) void
varied_dedupe_kernel_pair (unsigned faces_a, unsigned faces_b)
{
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
      TILE_ROW_EQ0 (); TILE_ROW_EQ0 (); TILE_ROW_EQ0 (); TILE_ROW_EQ0 ();
      TILE_ROW_EQ0 (); TILE_ROW_EQ0 (); TILE_ROW_EQ0 (); TILE_ROW_EQ0 ();
    }
  while (++face2 < faces_b);
}
