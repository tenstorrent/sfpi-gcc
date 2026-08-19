// WP15 renamed-equivalence twin: identical dataflow under fresh names
// (function and macro identifiers participate in no decision) -- the
// upward-carrier formation must fire identically to
// macro-planner-ims-carrier-fire-bh.C.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -mtt-tensix-macro-ims -mtt-tensix-macro-ims-carrier -fdump-rtl-rvtt_macro_planner" }
// { dg-final { scan-rtl-dump-times "Macro-planner upward-carrier: seed=2 chain=.3,4. reload-vd=3 prefix-clones=0 ii=13->12" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner upward-carrier: formed .ii=12, was 13." 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner descriptor-word dest=1: 0x94fe90d6" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner descriptor-word dest=2: 0x980009e0" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner formed: rows=8 runs=1" 1 "rvtt_macro_planner" } }
// { dg-final { scan-assembler-times "TTREPLAY\\t0, 8, 1, 1" 1 } }

#define STANZA(delta, offset)                                                 \
  do                                                                          \
    {                                                                          \
      __builtin_rvtt_sfppushc (0);                                            \
      __builtin_rvtt_sfppopc (0);                                             \
      auto left = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 4, 7);            \
      left = __builtin_rvtt_sfpcast (left, 3);                                \
      auto right = __builtin_rvtt_sfpload (nullptr, offset, 0, 0, 4, 7);      \
      auto spun = __builtin_rvtt_sfpshft_i (nullptr, right, delta, 0, 0, 0);  \
      auto cross = __builtin_rvtt_sfpmul24 (left, spun, 0);                   \
      right = __builtin_rvtt_sfpcast (right, 3);                              \
      auto low = __builtin_rvtt_sfpmul24 (left, right, 0);                    \
      auto high = __builtin_rvtt_sfpmul24 (left, right, 1);                   \
      low = __builtin_rvtt_sfpshft_i (nullptr, low, 1, 0, 0, 0);              \
      right = __builtin_rvtt_sfpshft_i (nullptr, right, delta, 0, 0, 0);      \
      high = __builtin_rvtt_sfpiadd_v (high, cross, 4);                       \
      right = __builtin_rvtt_sfpmul24 (left, right, 0);                       \
      high = __builtin_rvtt_sfpiadd_v (high, right, 4);                       \
      high = __builtin_rvtt_sfpshft_i (nullptr, high, 23, 0, 0, 0);           \
      low = __builtin_rvtt_sfpiadd_v (low, high, 4);                          \
      low = __builtin_rvtt_sfpcast (low, 3);                                  \
      __builtin_rvtt_sfpstore (nullptr, low, 0, 0, 0, 4, 7);                  \
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);                                   \
    }                                                                          \
  while (0)

__attribute__((noinline)) void woven_lattice_columns ()
{
  STANZA (-23, 64); STANZA (-23, 64); STANZA (-23, 64); STANZA (-23, 64);
  STANZA (-23, 64); STANZA (-23, 64); STANZA (-23, 64); STANZA (-23, 64);
}
#undef STANZA
