// WP9 near miss: the predicate's lane state reaches the row's store
// directly and survives PAST the row (a predicated store with no
// in-row all-lanes restore before it).  The restore-less
// definition-carrier structure matches no proven program; the row
// refuses by name and stays explicit.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump "descriptor-program-unproven" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "Macro-planner formed" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-not "\\.ttinsn" } }
// { dg-final { scan-assembler-not "SFPCONFIG" } }
// { dg-final { scan-assembler "SFPSETCC" } }

#define ROW()                                                                 \
  do                                                                          \
    {                                                                         \
      auto condition = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 2, 7);       \
      auto on_true = __builtin_rvtt_sfpload (nullptr, 32, 0, 0, 6, 7);        \
      __builtin_rvtt_sfppushc (0);                                            \
      __builtin_rvtt_sfpsetcc_v (condition, 2);                               \
      __builtin_rvtt_sfpstore (nullptr, on_true, 0, 0, 0, 6, 7);              \
      __builtin_rvtt_sfppopc (0);                                             \
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);                                   \
    }                                                                         \
  while (0)

__attribute__((noinline)) void predicated_store_no_restore ()
{
  ROW ();
  ROW ();
  ROW ();
  ROW ();
  ROW ();
  ROW ();
  ROW ();
  ROW ();
}
