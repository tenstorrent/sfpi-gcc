// Lane GJ window-pairing stride-phase generalization, the lane-GG
// interleaved-source keep twin: hand-interleaving two limb-2 rows into
// one source pair (the lane-FI interleave-2 precedent) LOSES macro
// formation entirely -- the planner refuses row-not-closed and the
// stream falls back to plain 21-word replay windows (10.5 words per
// row, WORSE than the macro-hosted 6+drain form; lane GG measured this
// empirically and banked the refusal).  The stride flag never reaches
// a tuner on this shape and is inert: no drain-interrow service, no
// window-pairing line.  The mechanism does not re-walk lane GG's
// refused interleave -- macro hosting keeps owning the row.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-optimize-window-pairing -mtt-tensix-optimize-window-pairing-stride -fdump-rtl-rvtt_macro_planner" }
// { dg-final { scan-rtl-dump-times "Macro-planner refusal: row-not-closed" 8 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "Macro-planner window-pairing" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "Macro-planner drain-interrow" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-times "TTREPLAY\\t0, 21, 1, 1" 1 } }
// { dg-final { scan-assembler-times "TTREPLAY\\t0, 21, 0, 0" 2 } }

#define PAIR()                                                                \
  do                                                                          \
    {                                                                          \
      __builtin_rvtt_sfppushc (0);                                            \
      __builtin_rvtt_sfppopc (0);                                             \
      auto a0 = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 4, 7);              \
      auto a1 = __builtin_rvtt_sfpload (nullptr, 2, 0, 0, 4, 7);              \
      a0 = __builtin_rvtt_sfpcast (a0, 3);                                    \
      a1 = __builtin_rvtt_sfpcast (a1, 3);                                    \
      auto b0 = __builtin_rvtt_sfpload (nullptr, 64, 0, 0, 4, 7);             \
      auto b1 = __builtin_rvtt_sfpload (nullptr, 66, 0, 0, 4, 7);             \
      b0 = __builtin_rvtt_sfpcast (b0, 3);                                    \
      b1 = __builtin_rvtt_sfpcast (b1, 3);                                    \
      auto hi0 = __builtin_rvtt_sfpmul24 (a0, b0, 1);                         \
      auto hi1 = __builtin_rvtt_sfpmul24 (a1, b1, 1);                         \
      auto lo0 = __builtin_rvtt_sfpmul24 (a0, b0, 0);                         \
      auto lo1 = __builtin_rvtt_sfpmul24 (a1, b1, 0);                         \
      hi0 = __builtin_rvtt_sfpshft_i (nullptr, hi0, 23, 0, 0, 0);             \
      hi1 = __builtin_rvtt_sfpshft_i (nullptr, hi1, 23, 0, 0, 0);             \
      lo0 = __builtin_rvtt_sfpiadd_v (lo0, hi0, 4);                           \
      lo1 = __builtin_rvtt_sfpiadd_v (lo1, hi1, 4);                           \
      lo0 = __builtin_rvtt_sfpcast (lo0, 3);                                  \
      lo1 = __builtin_rvtt_sfpcast (lo1, 3);                                  \
      __builtin_rvtt_sfpstore (nullptr, lo0, 0, 0, 0, 4, 7);                  \
      __builtin_rvtt_sfpstore (nullptr, lo1, 0, 0, 2, 4, 7);                  \
      __builtin_rvtt_ttincrwc (0, 4, 0, 0);                                   \
    }                                                                          \
  while (0)

__attribute__((noinline)) void limb2_interleaved_pairs ()
{
  PAIR (); PAIR (); PAIR (); PAIR ();
}
