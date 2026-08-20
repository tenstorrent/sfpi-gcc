// Derived-template SFPSHFT register-amount admission, varied surface
// (lane CZ): the BH arithmetic arm (mod 2, sign-propagating right
// shift for negative amounts -- the audited envelope's other member),
// different names and immediate.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -fdump-rtl-rvtt_macro_planner" }
// { dg-final { scan-rtl-dump-times "Macro-planner descriptor-word dest=0: 0x79ff70c5" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner descriptor-word dest=1: 0x7a0001d2" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner verify: ok" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner formed: rows=8 runs=1" 1 "rvtt_macro_planner" } }
// { dg-final { scan-assembler-not "SFPSHFT" } }
// { dg-final { scan-assembler-not "SFPIADD" } }

#define TILE()                                                                \
  do                                                                          \
    {                                                                          \
      __builtin_rvtt_sfppushc (0);                                            \
      __builtin_rvtt_sfppopc (0);                                             \
      auto expo = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 4, 7);            \
      expo = __builtin_rvtt_sfpiadd_i (nullptr, expo, -9, 0, 0, 4);           \
      auto mant = __builtin_rvtt_sfpload (nullptr, 64, 0, 0, 4, 7);           \
      mant = __builtin_rvtt_sfpshft_v (mant, expo, 2);                        \
      __builtin_rvtt_sfpstore (nullptr, mant, 0, 0, 0, 4, 7);                 \
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);                                   \
    }                                                                          \
  while (0)

__attribute__((noinline)) void arith_shift_rows ()
{
  TILE (); TILE (); TILE (); TILE ();
  TILE (); TILE (); TILE (); TILE ();
}
#undef TILE

int main ()
{
  arith_shift_rows ();
  return 0;
}
