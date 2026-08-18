// WP13 near-miss: the bounded repair search tries reduced hosted sets
// and every variant still refuses (the six-deep explicit accumulator
// chain saturates the Simple residues around every feasible event
// placement, and the UPPER-half product hosts only through the
// non-derivable legacy read rule).  The region refuses by the
// established names, no formation, no macro words -- a refused row is
// byte-identical code.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -mtt-tensix-macro-ims -fdump-rtl-rvtt_macro_planner" }
// { dg-final { scan-rtl-dump "Macro-planner schedule-candidate: ims-repair" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "Macro-planner formed:" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-not "\\.ttinsn" } }
// { dg-final { scan-assembler-not "SFPCONFIG" } }

#define ROW()                                                                 \
  do                                                                          \
    {                                                                          \
      __builtin_rvtt_sfppushc (0);                                            \
      __builtin_rvtt_sfppopc (0);                                             \
      auto a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 4, 7);               \
      a = __builtin_rvtt_sfpcast (a, 3);                                      \
      auto b = __builtin_rvtt_sfpload (nullptr, 64, 0, 0, 4, 7);              \
      b = __builtin_rvtt_sfpcast (b, 3);                                      \
      auto hi = __builtin_rvtt_sfpmul24 (a, b, 1);                            \
      hi = __builtin_rvtt_sfpiadd_v (hi, b, 4);                               \
      hi = __builtin_rvtt_sfpiadd_v (hi, b, 4);                               \
      hi = __builtin_rvtt_sfpiadd_v (hi, b, 4);                               \
      hi = __builtin_rvtt_sfpiadd_v (hi, b, 4);                               \
      hi = __builtin_rvtt_sfpiadd_v (hi, b, 4);                               \
      hi = __builtin_rvtt_sfpiadd_v (hi, b, 4);                               \
      b = __builtin_rvtt_sfpshft_i (nullptr, b, -23, 0, 0, 0);                \
      hi = __builtin_rvtt_sfpiadd_v (hi, b, 4);                               \
      hi = __builtin_rvtt_sfpcast (hi, 3);                                    \
      __builtin_rvtt_sfpstore (nullptr, hi, 0, 0, 0, 4, 7);                   \
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);                                   \
    }                                                                          \
  while (0)

__attribute__((noinline)) void exhausted_rows ()
{
  ROW (); ROW (); ROW (); ROW (); ROW (); ROW (); ROW (); ROW ();
}
#undef ROW
