// Derived-template SFPSHFT register-amount admission, capacity honesty
// (lane CZ): a row whose value carrier feeds TWO Simple-class members
// (an iadd-imm and the variable shift) cannot host both on one
// carrier's Simple slot; the planner hosts the amount-side iadd-imm
// and the shift, keeps the second iadd-imm EXPLICIT, and still forms
// -- partial hosting stays honest instead of over-claiming placement.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -fdump-rtl-rvtt_macro_planner" }
// { dg-final { scan-rtl-dump-times "Macro-planner descriptor-word dest=0: 0x79fe10c5" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner descriptor-word dest=1: 0x7a0001d0" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner verify: ok" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner formed: rows=8 runs=1" 1 "rvtt_macro_planner" } }
// The unhosted second iadd-imm survives as the row's one explicit
// compute issue.
// { dg-final { scan-assembler-times "SFPIADD" 8 } }
// { dg-final { scan-assembler-not "SFPSHFT" } }

__attribute__((noinline)) void double_simple_rows ()
{
#define ROW()                                                                 \
  do                                                                          \
    {                                                                          \
      __builtin_rvtt_sfppushc (0);                                            \
      __builtin_rvtt_sfppopc (0);                                             \
      auto amt = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 4, 7);             \
      amt = __builtin_rvtt_sfpiadd_i (nullptr, amt, -31, 0, 0, 4);            \
      auto v = __builtin_rvtt_sfpload (nullptr, 64, 0, 0, 4, 7);              \
      v = __builtin_rvtt_sfpiadd_i (nullptr, v, 1, 0, 0, 4);                  \
      v = __builtin_rvtt_sfpshft_v (v, amt, 0);                               \
      __builtin_rvtt_sfpstore (nullptr, v, 0, 0, 0, 4, 7);                    \
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);                                   \
    }                                                                          \
  while (0)
  ROW (); ROW (); ROW (); ROW (); ROW (); ROW (); ROW (); ROW ();
#undef ROW
}

int main ()
{
  double_simple_rows ();
  return 0;
}
