// Derived-template SFPSHFT register-amount admission (the
// enumerated vocabulary): the variable-shift form (mod 0, the audited
// envelope's logical arm) shifts LReg[VD] in place by the SIGNED
// amount in LReg[VC] (SFPSHFT.md BH functional model; the reference simulator
// TENSIX_EXECUTE_SFPSHFT), so an in-place row member v = v << amt
// realizes exactly as an SFPSHFT template: VD supplied by the launch,
// the amount register surviving as the encoded VC under route 1,
// imm12 packing the 0 the decode requires.  The amount here is
// another carrier's iadd-imm result (the SFPIADD-immediate
// class), so the row composes both new words into one two-carrier
// calendar -- the gcd-fresh round's strip step shape
// (b >>= ctz: lz-derived amount, then the variable shift).
// Before the admission the row refused derivation
// (descriptor-program-unproven) and kept the explicit SFPIADD and
// SFPSHFT.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -fdump-rtl-rvtt_macro_planner" }
// { dg-final { scan-rtl-dump-times "Macro-planner descriptor-word dest=0: 0x79fe10c5" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner descriptor-word dest=1: 0x7a0001d0" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner verify: ok" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner formed: rows=8 runs=1" 1 "rvtt_macro_planner" } }
// { dg-final { scan-assembler-not "SFPSHFT" } }
// { dg-final { scan-assembler-not "SFPIADD" } }

#define ROW()                                                                 \
  do                                                                          \
    {                                                                          \
      __builtin_rvtt_sfppushc (0);                                            \
      __builtin_rvtt_sfppopc (0);                                             \
      auto amt = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 4, 7);             \
      amt = __builtin_rvtt_sfpiadd_i (nullptr, amt, -31, 0, 0, 4);            \
      auto v = __builtin_rvtt_sfpload (nullptr, 64, 0, 0, 4, 7);              \
      v = __builtin_rvtt_sfpshft_v (v, amt, 0);                               \
      __builtin_rvtt_sfpstore (nullptr, v, 0, 0, 0, 4, 7);                    \
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);                                   \
    }                                                                          \
  while (0)

__attribute__((noinline)) void shift_by_lane_rows ()
{
  ROW (); ROW (); ROW (); ROW (); ROW (); ROW (); ROW (); ROW ();
}
#undef ROW

int main ()
{
  shift_by_lane_rows ();
  return 0;
}
