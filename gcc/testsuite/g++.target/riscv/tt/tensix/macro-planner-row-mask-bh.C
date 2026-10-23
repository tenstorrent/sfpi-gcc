// A full physical lane proof requires both flags and LaneConfig ROW_MASK.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump-times "Macro-planner formed: rows=8 runs=1" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "cc-enable-unproved" 3 "rvtt_macro_planner" } }

#include "rtl-lane-reset.h"

#define ROW() do { \
  __builtin_rvtt_sfppushc (0); \
  __builtin_rvtt_sfppopc (0); \
  auto v = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7); \
  auto s = __builtin_rvtt_sfpshft_i (nullptr, v, -31, 0, 0, 0); \
  auto r = __builtin_rvtt_sfpcast (s, 0); \
  __builtin_rvtt_sfpstore (nullptr, r, 0, 0, 0, 0, 7); \
  __builtin_rvtt_ttincrwc (0, 2, 0, 0); \
} while (0)
#define ROWS() do { ROW (); ROW (); ROW (); ROW (); \
  ROW (); ROW (); ROW (); ROW (); } while (0)

void unknown_entry () { ROWS (); }
void flags_only () { __builtin_rvtt_sfpencc (10, 3); ROWS (); }
void nonzero_row_mask ()
{
  RVTT_TEST_LANE_RESET ();
  __builtin_rvtt_sfpconfig_i (1, 15, 1);
  __builtin_rvtt_sfpencc (10, 3);
  ROWS ();
}
void explicit_reset () { RVTT_TEST_LANE_RESET (); ROWS (); }
