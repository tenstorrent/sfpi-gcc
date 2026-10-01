// Derived-template Simple-unary admission, SFPEXEXP:
// exexp takes NO operand override (the reference simulator's build_dispatch has no arm for
// opcode 0x77), so its source is always NAME-encoded -- here the
// second carrier's loaded register survives as the template VC and
// the exponent feeds an in-place hosted iadd-reg accumulate on the
// first carrier (the established class): a two-carrier composed
// calendar with zero explicit compute issues.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -fdump-rtl-rvtt_macro_planner" }
// { dg-final { scan-rtl-dump-times "Macro-planner descriptor-word dest=0: 0x770001c0" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner descriptor-word dest=1: 0x790001d4" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner verify: ok" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner formed: rows=8 runs=1" 1 "rvtt_macro_planner" } }
// { dg-final { scan-assembler-not "SFPEXEXP" } }
// { dg-final { scan-assembler-not "SFPIADD" } }

#define ROW()                                                                 \
  do                                                                          \
    {                                                                          \
      __builtin_rvtt_sfppushc (0);                                            \
      __builtin_rvtt_sfppopc (0);                                             \
      auto x = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 4, 7);               \
      auto y = __builtin_rvtt_sfpload (nullptr, 64, 0, 0, 4, 7);              \
      auto e = __builtin_rvtt_sfpexexp (y, 0);                                \
      x = __builtin_rvtt_sfpiadd_v (x, e, 4);                                 \
      __builtin_rvtt_sfpstore (nullptr, x, 0, 0, 0, 4, 7);                    \
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);                                   \
    }                                                                          \
  while (0)

__attribute__((noinline)) void exexp_rows ()
{
  ROW (); ROW (); ROW (); ROW (); ROW (); ROW (); ROW (); ROW ();
}
#undef ROW

int main ()
{
  exexp_rows ();
  return 0;
}
