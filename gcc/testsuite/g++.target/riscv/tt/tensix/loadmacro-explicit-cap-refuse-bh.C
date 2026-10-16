// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -fdump-rtl-rvtt_macro_planner" }
// Check that more than 16 constraint-carrying explicit issues refuse the macro (descriptor-explicit-issue-cap-exceeded).
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
