// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -fdump-rtl-rvtt_macro_planner" }
// Explicit-issue cap fail-closed twin (from an
// adversarial audit 4.4): the descriptor builder silently TRUNCATED
// the explicit-issue record at an 8-entry array bound, so derive-core's
// silent-discard hazard rule judged calendars against an INCOMPLETE
// constraint set -- live on the committed intmul family, whose rows
// carry 9-10 constraint-carrying explicits (the complete set derives
// the identical calendar there, proving those fires benign).  The
// fixed builder records every CONSTRAINT-CARRYING (nonzero sub-unit
// mask) explicit up to the architectural sequencer bound of 16 and
// refuses BY NAME beyond it; the earlier binary carried this row's 20
// explicits into derivation with no cap complaint and only refused
// downstream under an unrelated generic name.
// { dg-final { scan-rtl-dump "Macro-planner descriptor-refusal: descriptor-explicit-issue-cap-exceeded" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "Macro-planner formed" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-not "SFPLOADMACRO" } }

constexpr unsigned ni = 7;	/* BH no-increment addr mode */

#define ROW()                                                              \
  do                                                                       \
    {                                                                      \
      auto a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, ni);           \
      auto b = __builtin_rvtt_sfpload (nullptr, 64, 0, 0, 0, ni);          \
      auto pair = __builtin_rvtt_sfpswap (a, b, 1);                        \
      auto r = __builtin_rvtt_sfpselect2 (pair, 0);                        \
      r = __builtin_rvtt_sfpmad (r, a, b, 0);                              \
      r = __builtin_rvtt_sfpmad (r, b, a, 0);                              \
      r = __builtin_rvtt_sfpmad (r, a, b, 0);                              \
      r = __builtin_rvtt_sfpmad (r, b, a, 0);                              \
      r = __builtin_rvtt_sfpmad (r, a, b, 0);                              \
      r = __builtin_rvtt_sfpmad (r, b, a, 0);                              \
      r = __builtin_rvtt_sfpmad (r, a, b, 0);                              \
      r = __builtin_rvtt_sfpmad (r, b, a, 0);                              \
      r = __builtin_rvtt_sfpmad (r, a, b, 0);                              \
      r = __builtin_rvtt_sfpmad (r, b, a, 0);                              \
      r = __builtin_rvtt_sfpmad (r, a, b, 0);                              \
      r = __builtin_rvtt_sfpmad (r, b, a, 0);                              \
      r = __builtin_rvtt_sfpmad (r, a, b, 0);                              \
      r = __builtin_rvtt_sfpmad (r, b, a, 0);                              \
      r = __builtin_rvtt_sfpmad (r, a, b, 0);                              \
      r = __builtin_rvtt_sfpmad (r, b, a, 0);                              \
      r = __builtin_rvtt_sfpmad (r, a, b, 0);                              \
      __builtin_rvtt_sfpstore (nullptr, r, 0, 0, 0, 0, ni);                \
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);                                \
    }                                                                      \
  while (0)

__attribute__((noinline)) void
explicit_cap_kernel ()
{
  ROW (); ROW (); ROW (); ROW (); ROW (); ROW (); ROW (); ROW ();
}
