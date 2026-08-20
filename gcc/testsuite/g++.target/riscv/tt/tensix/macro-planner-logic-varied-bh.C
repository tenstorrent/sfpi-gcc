// Derived-template logic-family admission, varied surface (lane CZ):
// the SFPXOR tied in-place form (no operand override exists for
// opcode 0x8d -- the named VC survives verbatim) and the SFPOR
// USE_VB form with the operand order flipped.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -fdump-rtl-rvtt_macro_planner" }
// { dg-final { scan-rtl-dump-times "Macro-planner descriptor-word dest=0: 0x8d0001c0" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner descriptor-word dest=0: 0x7f0001c1" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner verify: ok" 2 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner formed: rows=8 runs=1" 2 "rvtt_macro_planner" } }
// { dg-final { scan-assembler-not "SFPXOR" } }
// { dg-final { scan-assembler-not "SFPOR" } }

__attribute__((noinline)) void parity_rows ()
{
#define ROW()                                                                 \
  do                                                                          \
    {                                                                          \
      __builtin_rvtt_sfppushc (0);                                            \
      __builtin_rvtt_sfppopc (0);                                             \
      auto a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 4, 7);               \
      auto b = __builtin_rvtt_sfpload (nullptr, 64, 0, 0, 4, 7);              \
      auto c = __builtin_rvtt_sfpxor (a, b);                                  \
      __builtin_rvtt_sfpstore (nullptr, c, 0, 0, 0, 4, 7);                    \
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);                                   \
    }                                                                          \
  while (0)
  ROW (); ROW (); ROW (); ROW (); ROW (); ROW (); ROW (); ROW ();
#undef ROW
}

__attribute__((noinline)) void union_rows ()
{
#define ROW()                                                                 \
  do                                                                          \
    {                                                                          \
      __builtin_rvtt_sfppushc (0);                                            \
      __builtin_rvtt_sfppopc (0);                                             \
      auto a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 4, 7);               \
      auto b = __builtin_rvtt_sfpload (nullptr, 64, 0, 0, 4, 7);              \
      auto c = __builtin_rvtt_sfpor (b, a);                                   \
      __builtin_rvtt_sfpstore (nullptr, c, 0, 0, 0, 4, 7);                    \
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);                                   \
    }                                                                          \
  while (0)
  ROW (); ROW (); ROW (); ROW (); ROW (); ROW (); ROW (); ROW ();
#undef ROW
}

int main ()
{
  parity_rows ();
  union_rows ();
  return 0;
}
