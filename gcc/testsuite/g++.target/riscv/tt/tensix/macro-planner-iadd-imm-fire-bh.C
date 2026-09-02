// Derived-template SFPIADD-immediate admission (enumerated
// vocabulary): the immediate form's functional model reads ONLY
// LReg[VC] and SignExtend(Imm12) (SFPIADD.md; the reference simulator
// TENSIX_EXECUTE_SFPIADD), so the in-place row member
// v = v + imm realizes exactly as an SFPIADD template with the
// immediate riding the template imm12 field, src_c 0, and the VC:=VD
// route supplying the launch value.  Before the admission this row
// refused derivation (descriptor-program-unproven) and kept the
// explicit SFPIADD; with it the row forms a single-launch three-slot
// calendar (Simple iadd-imm + delayed store) at a one-row interval.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -fdump-rtl-rvtt_macro_planner" }
// { dg-final { scan-rtl-dump-times "Macro-planner descriptor: derived-calendar events=2 staging=none drain=2 kind-mask=0x0" 1 "rvtt_macro_planner" } }
// The derived word: SFPIADD, imm12 = -31 (0xfe1), VC field 0 (the
// launch-VD route), mod1 5 = ARG_IMM | CC_NONE.
// { dg-final { scan-rtl-dump-times "Macro-planner descriptor-word dest=0: 0x79fe10c5" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner verify: ok" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner formed: rows=8 runs=1" 1 "rvtt_macro_planner" } }
// { dg-final { scan-assembler-not "SFPIADD" } }

#define ROW()                                                                 \
  do                                                                          \
    {                                                                          \
      __builtin_rvtt_sfppushc (0);                                            \
      __builtin_rvtt_sfppopc (0);                                             \
      auto a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 4, 7);               \
      a = __builtin_rvtt_sfpiadd_i (nullptr, a, -31, 0, 0, 4);                \
      __builtin_rvtt_sfpstore (nullptr, a, 0, 0, 0, 4, 7);                    \
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);                                   \
    }                                                                          \
  while (0)

__attribute__((noinline)) void iadd_imm_rows ()
{
  ROW (); ROW (); ROW (); ROW (); ROW (); ROW (); ROW (); ROW ();
}
#undef ROW

int main ()
{
  iadd_imm_rows ();
  return 0;
}
