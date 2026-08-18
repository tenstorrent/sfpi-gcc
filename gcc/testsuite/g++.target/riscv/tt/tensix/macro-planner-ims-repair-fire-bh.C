// WP13 (IMS placement repair, literature Idea 5 -- Rau): the established
// all-or-nothing search refuses this integer row on every grouping (the
// in-place VB-factor SFPMUL24's write-after-read floor against the six
// chained explicit partial products pushes hosted-event execution past
// the architectural delay range / derived-class envelope).  Under
// -mtt-tensix-macro-ims the candidate space continues with
// deterministically reduced hosted sets; the first proven variant is the
// stores-demoted grouping with the in-place SFPMUL24 unplaced (banned
// item 13), which derives, verifies, and forms.  The banned set and the
// greedy hosted count are dump API.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -mtt-tensix-macro-ims -fdump-rtl-rvtt_macro_planner" }
// { dg-final { scan-rtl-dump "Macro-planner schedule-candidate: ims-repair grouping=1 banned-items=.13. greedy-hosted=5" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner verify: ok" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner formed: rows=8 runs=1" 1 "rvtt_macro_planner" } }
// The shared in-place cast template, the SHFT2 immediate realization,
// and the store-producer cast survive in the repaired calendar:
// { dg-final { scan-rtl-dump-times "Macro-planner descriptor-word dest=0: 0x900000c3" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner descriptor-word dest=1: 0x94fe90d6" 1 "rvtt_macro_planner" } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2468667392" 8 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2470764608" 8 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2478096384" 8 } }

#define ROW()                                                                 \
  do                                                                          \
    {                                                                          \
      __builtin_rvtt_sfppushc (0);                                            \
      __builtin_rvtt_sfppopc (0);                                             \
      auto a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 4, 7);               \
      a = __builtin_rvtt_sfpcast (a, 3);                                      \
      auto b = __builtin_rvtt_sfpload (nullptr, 64, 0, 0, 4, 7);              \
      b = __builtin_rvtt_sfpcast (b, 3);                                      \
      auto lo = __builtin_rvtt_sfpmul24 (a, b, 0);                            \
      auto hi = __builtin_rvtt_sfpmul24 (a, b, 1);                            \
      lo = __builtin_rvtt_sfpmul24 (b, lo, 0);                                \
      lo = __builtin_rvtt_sfpmul24 (b, lo, 0);                                \
      lo = __builtin_rvtt_sfpmul24 (b, lo, 0);                                \
      auto sa = __builtin_rvtt_sfpshft_i (nullptr, a, -23, 0, 0, 0);          \
      auto c0 = __builtin_rvtt_sfpmul24 (sa, b, 0);                           \
      b = __builtin_rvtt_sfpshft_i (nullptr, b, -23, 0, 0, 0);                \
      hi = __builtin_rvtt_sfpiadd_v (hi, c0, 4);                              \
      b = __builtin_rvtt_sfpmul24 (a, b, 0);                                  \
      hi = __builtin_rvtt_sfpiadd_v (hi, b, 4);                               \
      hi = __builtin_rvtt_sfpshft_i (nullptr, hi, 23, 0, 0, 0);               \
      lo = __builtin_rvtt_sfpiadd_v (lo, hi, 4);                              \
      lo = __builtin_rvtt_sfpcast (lo, 3);                                    \
      __builtin_rvtt_sfpstore (nullptr, lo, 0, 0, 0, 4, 7);                   \
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);                                   \
    }                                                                          \
  while (0)

__attribute__((noinline)) void repair_fire_rows ()
{
  ROW (); ROW (); ROW (); ROW (); ROW (); ROW (); ROW (); ROW ();
}
#undef ROW
