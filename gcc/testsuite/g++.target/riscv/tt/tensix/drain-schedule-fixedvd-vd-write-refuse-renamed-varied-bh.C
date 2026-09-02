// Drain-elision genericity twin: renamed symbols, a different Dst
// address, and a different (still encodable) shift amount -- the
// follower VD-write refusal keys on the derived launch plan (fixed-VD
// value carrier, launch issue inside the decoded pending horizon),
// never on names, addresses, or coefficient values.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-optimize-drain-schedule -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump "Macro-planner drain-refusal: drain-follower-vd-write" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner drain-interrow: drain=3 rows=8" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "drain elided" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "drain-elided" "rvtt_macro_planner" } }

#define STANZA()                                                              \
  do                                                                          \
    {                                                                          \
      __builtin_rvtt_sfppushc (0);                                            \
      __builtin_rvtt_sfppopc (0);                                             \
      auto picked = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 4, 7);          \
      auto moved = __builtin_rvtt_sfpshft_i (nullptr, picked, -30, 0, 0, 0);  \
      auto widened = __builtin_rvtt_sfpcast (moved, 0);                       \
      __builtin_rvtt_sfpstore (nullptr, widened, 0, 0, 0, 4, 7);              \
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);                                   \
    }                                                                          \
  while (0)

__attribute__((noinline)) void exponent_walk_hex ()
{
  STANZA ();
  STANZA ();
  STANZA ();
  STANZA ();
  __builtin_rvtt_ttincrwc (0, 4, 0, 0);
  STANZA ();
  STANZA ();
  STANZA ();
  STANZA ();
}

#undef STANZA
