// Derived-template Simple-unary admission, varied surface.  The SFPMOV
// negate arm (mod 1) remains an admitted in-place single-carrier calendar.
// Blackhole silicon disproved lane-correct macro execution of SFPABS, so
// its otherwise identical row shape must remain explicit.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -fdump-rtl-rvtt_macro_planner" }
// { dg-final { scan-rtl-dump-times "Macro-planner descriptor-word dest=0: 0x7c0000c1" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "Macro-planner descriptor-word dest=0: 0x7d0000c1" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "subunit-placement-unproven" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner verify: ok" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner formed: rows=8 runs=1" 1 "rvtt_macro_planner" } }
// { dg-final { scan-assembler-not "SFPMOV" } }
// { dg-final { scan-assembler-times "SFPABS" 1 } }

__attribute__((noinline)) void neg_rows ()
{
#define ROW()                                                                 \
  do                                                                          \
    {                                                                          \
      __builtin_rvtt_sfppushc (0);                                            \
      __builtin_rvtt_sfppopc (0);                                             \
      auto a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 4, 7);               \
      a = __builtin_rvtt_sfpmov (a, 1);                                       \
      __builtin_rvtt_sfpstore (nullptr, a, 0, 0, 0, 4, 7);                    \
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);                                   \
    }                                                                          \
  while (0)
  ROW (); ROW (); ROW (); ROW (); ROW (); ROW (); ROW (); ROW ();
#undef ROW
}

__attribute__((noinline)) void abs_rows ()
{
#define ROW()                                                                 \
  do                                                                          \
    {                                                                          \
      __builtin_rvtt_sfppushc (0);                                            \
      __builtin_rvtt_sfppopc (0);                                             \
      auto a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 4, 7);               \
      a = __builtin_rvtt_sfpabs (a, 1);                                       \
      __builtin_rvtt_sfpstore (nullptr, a, 0, 0, 0, 4, 7);                    \
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);                                   \
    }                                                                          \
  while (0)
  ROW (); ROW (); ROW (); ROW (); ROW (); ROW (); ROW (); ROW ();
#undef ROW
}

int main ()
{
  neg_rows ();
  abs_rows ();
  return 0;
}
