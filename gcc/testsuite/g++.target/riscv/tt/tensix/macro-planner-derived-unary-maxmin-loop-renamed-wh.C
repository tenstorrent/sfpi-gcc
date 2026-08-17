// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -fdump-rtl-rvtt_macro_planner-details" }
// WH renamed/varied twin of the derived unary max/min loop formation:
// different function name, a different constant register (L11), a
// different Dst address, the opposite operand layout, and the other
// CPU.  The derived sequence words are the same architectural facts;
// the swap template's fields re-derive from the varied operands
// (constant-in-VC keeps the source's mod sense).  Proves the
// derivation is value- and name-independent.
// { dg-final { scan-rtl-dump "sequence-derivation-hazard" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "descriptor: derived-calendar events=2 staging=copy drain=3 kind-mask=0x0" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "descriptor-word dest=0: 0x92000bc1" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "descriptor-word dest=1: 0x940000d6" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "descriptor-word dest=4: 0x00d50084" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "descriptor-word dest=5: 0x53000000" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "descriptor-word dest=8: 0x00000020" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner verify: ok" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner formed: rows=8" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-not "SFPSWAP" } }
// { dg-final { scan-assembler-not "SFPSTORE" } }
// { dg-final { scan-assembler-not "TTINCRWC" } }

__attribute__((noinline)) void pelican_floor_rows ()
{
#pragma GCC unroll 8
  for (int row = 0; row < 32; ++row)
    {
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfppopc (0);
      auto v = __builtin_rvtt_sfpload (nullptr, 64, 0, 0, 0, 3);
      auto cst = __builtin_rvtt_sfpreadlreg (11);
      auto pair = __builtin_rvtt_sfpswap (v, cst, 1);
      auto r = __builtin_rvtt_sfpselect2 (pair, 0);
      __builtin_rvtt_sfpstore (nullptr, r, 64, 0, 0, 0, 3);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
