// Renamed twin of macro-planner-ims-repair-fire-bh.C: different function
// and variable names, different Dst addresses.  The repair decision is
// derived from typed effects and the capability tables alone, so the
// classification -- stores-demoted grouping, in-place-SFPMUL24 unplaced,
// formation proven -- must be identical; only the address-derived launch
// words may differ.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -mtt-tensix-macro-ims -fdump-rtl-rvtt_macro_planner" }
// { dg-final { scan-rtl-dump "Macro-planner schedule-candidate: ims-repair grouping=1 banned-items=.13. greedy-hosted=5" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner verify: ok" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner formed: rows=8 runs=1" 1 "rvtt_macro_planner" } }

#define STANZA()                                                              \
  do                                                                          \
    {                                                                          \
      __builtin_rvtt_sfppushc (0);                                            \
      __builtin_rvtt_sfppopc (0);                                             \
      auto north = __builtin_rvtt_sfpload (nullptr, 32, 0, 0, 4, 7);          \
      north = __builtin_rvtt_sfpcast (north, 3);                              \
      auto south = __builtin_rvtt_sfpload (nullptr, 96, 0, 0, 4, 7);          \
      south = __builtin_rvtt_sfpcast (south, 3);                              \
      auto keel = __builtin_rvtt_sfpmul24 (north, south, 0);                  \
      auto mast = __builtin_rvtt_sfpmul24 (north, south, 1);                  \
      keel = __builtin_rvtt_sfpmul24 (south, keel, 0);                        \
      keel = __builtin_rvtt_sfpmul24 (south, keel, 0);                        \
      keel = __builtin_rvtt_sfpmul24 (south, keel, 0);                        \
      auto jib = __builtin_rvtt_sfpshft_i (nullptr, north, -23, 0, 0, 0);     \
      auto boom = __builtin_rvtt_sfpmul24 (jib, south, 0);                    \
      south = __builtin_rvtt_sfpshft_i (nullptr, south, -23, 0, 0, 0);        \
      mast = __builtin_rvtt_sfpiadd_v (mast, boom, 4);                        \
      south = __builtin_rvtt_sfpmul24 (north, south, 0);                      \
      mast = __builtin_rvtt_sfpiadd_v (mast, south, 4);                       \
      mast = __builtin_rvtt_sfpshft_i (nullptr, mast, 23, 0, 0, 0);           \
      keel = __builtin_rvtt_sfpiadd_v (keel, mast, 4);                        \
      keel = __builtin_rvtt_sfpcast (keel, 3);                                \
      __builtin_rvtt_sfpstore (nullptr, keel, 32, 0, 0, 4, 7);                \
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);                                   \
    }                                                                          \
  while (0)

__attribute__((noinline)) void regatta_rows ()
{
  STANZA (); STANZA (); STANZA (); STANZA ();
  STANZA (); STANZA (); STANZA (); STANZA ();
}
#undef STANZA
