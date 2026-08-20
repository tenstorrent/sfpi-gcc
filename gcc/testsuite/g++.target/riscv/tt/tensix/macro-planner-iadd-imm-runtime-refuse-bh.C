// Derived-template SFPIADD-immediate admission, fail-closed twin
// (lane CZ): a RUNTIME immediate takes the register-argument
// alternative of rvtt_sfpiadd_i_lv_int (the runtime-synthesized
// instruction push), which is outside the pattern's audited effect
// envelope and outside the admitted class -- the region refuses by
// name and the explicit runtime-composed SFPIADD sequence survives.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -fdump-rtl-rvtt_macro_planner" }
// { dg-final { scan-rtl-dump "Macro-planner refusal: row-opaque-effect" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "Macro-planner formed" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-times "SFPIADD" 8 } }

__attribute__((noinline)) void runtime_imm_rows (int k)
{
#define ROW()                                                                 \
  do                                                                          \
    {                                                                          \
      __builtin_rvtt_sfppushc (0);                                            \
      __builtin_rvtt_sfppopc (0);                                             \
      auto a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 4, 7);               \
      a = __builtin_rvtt_sfpiadd_i (nullptr, a, k, 0, 0, 4);                  \
      __builtin_rvtt_sfpstore (nullptr, a, 0, 0, 0, 4, 7);                    \
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);                                   \
    }                                                                          \
  while (0)
  ROW (); ROW (); ROW (); ROW (); ROW (); ROW (); ROW (); ROW ();
#undef ROW
}

int main ()
{
  runtime_imm_rows (3);
  return 0;
}
