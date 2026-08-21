// Lane EV genericity twin: renamed symbols, a different row count, a
// different Dst address, and a different (still encodable) shift
// amount -- the inter-row drain keys on the derived launch plan
// (fixed-VD value carrier, drain > 0), never on names, trip counts, or
// coefficient values.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump-times "Macro-planner drain-interrow: drain=3 rows=4" 1 "rvtt_macro_planner" } }
// { dg-final { scan-assembler-times "SFPNOP" 12 } }
// { dg-final { scan-assembler-times "SFPCONFIG" 4 } }
// { dg-final { scan-assembler-not "SFPSHFT" } }
// { dg-final { scan-assembler-not "SFPCAST" } }

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

__attribute__((noinline)) void exponent_hoist_quad ()
{
  STANZA ();
  STANZA ();
  STANZA ();
  STANZA ();
}

#undef STANZA
