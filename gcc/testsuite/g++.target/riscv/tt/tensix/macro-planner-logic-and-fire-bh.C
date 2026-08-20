// Derived-template logic-family admission, SFPAND (lane CZ,
// enumerated vocabulary): the BH three-operand USE_VB form names one
// source through the VB subfield (imm12 bits 3:0 -- L0 is a legal VB
// name; the unused-code collision is a VC-field convention only) and
// the other as the encoded VC, with VD write-only and supplied by the
// launch -- the gcd-fresh iso/common mask shape (a & b, a | b of two
// loaded values).  Before the admission this row refused derivation
// (descriptor-program-unproven) and kept the explicit SFPAND.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -fdump-rtl-rvtt_macro_planner" }
// The derived word: SFPAND mod1 USE_VB, VB subfield naming L0, VC
// naming L1.
// { dg-final { scan-rtl-dump-times "Macro-planner descriptor-word dest=0: 0x7e0001c1" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner verify: ok" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner formed: rows=8 runs=1" 1 "rvtt_macro_planner" } }
// { dg-final { scan-assembler-not "SFPAND" } }

#define ROW()                                                                 \
  do                                                                          \
    {                                                                          \
      __builtin_rvtt_sfppushc (0);                                            \
      __builtin_rvtt_sfppopc (0);                                             \
      auto a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 4, 7);               \
      auto b = __builtin_rvtt_sfpload (nullptr, 64, 0, 0, 4, 7);              \
      auto c = __builtin_rvtt_sfpand (a, b);                                  \
      __builtin_rvtt_sfpstore (nullptr, c, 0, 0, 0, 4, 7);                    \
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);                                   \
    }                                                                          \
  while (0)

__attribute__((noinline)) void mask_rows ()
{
  ROW (); ROW (); ROW (); ROW (); ROW (); ROW (); ROW (); ROW ();
}
#undef ROW

int main ()
{
  mask_rows ();
  return 0;
}
