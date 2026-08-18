// Varied-constants twin of macro-planner-ims-repair-fire-bh.C: a
// different immediate shift distance (-21) and the subtract integer-add
// modifier (6).  The repair still proves the same reduced hosted set,
// and the derived SHFT2 template word visibly differs from the fire
// test's -23 encoding -- constants flow into derived words, never into
// the decision.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -mtt-tensix-macro-ims -fdump-rtl-rvtt_macro_planner" }
// { dg-final { scan-rtl-dump "Macro-planner schedule-candidate: ims-repair grouping=1 banned-items=.13. greedy-hosted=5" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner verify: ok" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner formed: rows=8 runs=1" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "Macro-planner descriptor-word dest=1: 0x94fe90d6" "rvtt_macro_planner" } }

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
      auto sa = __builtin_rvtt_sfpshft_i (nullptr, a, -21, 0, 0, 0);          \
      auto c0 = __builtin_rvtt_sfpmul24 (sa, b, 0);                           \
      b = __builtin_rvtt_sfpshft_i (nullptr, b, -21, 0, 0, 0);                \
      hi = __builtin_rvtt_sfpiadd_v (hi, c0, 6);                              \
      b = __builtin_rvtt_sfpmul24 (a, b, 0);                                  \
      hi = __builtin_rvtt_sfpiadd_v (hi, b, 6);                               \
      hi = __builtin_rvtt_sfpshft_i (nullptr, hi, 21, 0, 0, 0);               \
      lo = __builtin_rvtt_sfpiadd_v (lo, hi, 6);                              \
      lo = __builtin_rvtt_sfpcast (lo, 3);                                    \
      __builtin_rvtt_sfpstore (nullptr, lo, 0, 0, 0, 4, 7);                   \
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);                                   \
    }                                                                          \
  while (0)

__attribute__((noinline)) void varied_product_rows ()
{
  ROW (); ROW (); ROW (); ROW (); ROW (); ROW (); ROW (); ROW ();
}
#undef ROW
