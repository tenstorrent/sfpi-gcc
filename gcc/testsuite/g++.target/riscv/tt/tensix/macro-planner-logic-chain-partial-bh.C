// Derived-template vocabulary composition honesty (lane CZ): the
// gcd-fresh common-shift chain (m = a | b; m = lz(m); m += -31) puts
// THREE Simple-class members on one register web -- a carrier hosts
// at most one Simple event, so the planner hosts the iadd-imm, keeps
// the or and lz explicit, and still forms.  Partial hosting of a
// deep in-place chain is the designed outcome, not a defect: each
// member is individually inside the admitted vocabulary.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -fdump-rtl-rvtt_macro_planner" }
// { dg-final { scan-rtl-dump-times "Macro-planner descriptor-word dest=0: 0x79fe10c5" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner verify: ok" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner formed: rows=8 runs=1" 1 "rvtt_macro_planner" } }
// { dg-final { scan-assembler "SFPOR" } }
// { dg-final { scan-assembler "SFPLZ" } }
// { dg-final { scan-assembler-not "SFPIADD" } }

#define ROW()                                                                 \
  do                                                                          \
    {                                                                          \
      __builtin_rvtt_sfppushc (0);                                            \
      __builtin_rvtt_sfppopc (0);                                             \
      auto a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 4, 7);               \
      auto b = __builtin_rvtt_sfpload (nullptr, 64, 0, 0, 4, 7);              \
      auto m = __builtin_rvtt_sfpor (a, b);                                   \
      m = __builtin_rvtt_sfplz (m, 0);                                        \
      m = __builtin_rvtt_sfpiadd_i (nullptr, m, -31, 0, 0, 4);                \
      __builtin_rvtt_sfpstore (nullptr, m, 0, 0, 0, 4, 7);                    \
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);                                   \
    }                                                                          \
  while (0)

__attribute__((noinline)) void common_shift_rows ()
{
  ROW (); ROW (); ROW (); ROW (); ROW (); ROW (); ROW (); ROW ();
}
#undef ROW

int main ()
{
  common_shift_rows ();
  return 0;
}
