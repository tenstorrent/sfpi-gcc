// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -fdump-rtl-rvtt_macro_planner-details" }
// Derived-calendar FORMATION of the unary max/min loop shape (the
// blocked class this increment opens): SFPLOAD / constant-register
// SFPSWAP / SFPSTORE, eight rows per loop body.  No proven whole-word
// program matches; the calendar derives from the schedule and the
// established architectural facts (docs/TIMING_CALENDAR_DERIVATION.md):
// the merged ii=1 candidate refuses the SFPSWAP adjacency rule, the
// store-demoted candidate derives swap-at-delay-0, the staging copy on
// the Round sub-unit at delay 2 ((*) the same transient-copy
// realization as the frozen minmax calendar), and the LReg16 store at
// delay 2 -- sequence words 0x00d50084 / 0x53000000, field-derived
// misc 0x020 (store takes the store-only carrier's launch Mod0; all
// delays cycle-counted).  Both swap senses derive from the operand
// layout: constant-in-VD flips the result routing to mod 9,
// constant-in-VC keeps mod 1.
// { dg-final { scan-rtl-dump-times "sequence-derivation-hazard" 2 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "descriptor: derived-calendar events=2 staging=copy drain=3 kind-mask=0x0" 2 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "descriptor-word dest=0: 0x920009c9" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "descriptor-word dest=0: 0x920009c1" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "descriptor-word dest=1: 0x940000d6" 2 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "descriptor-word dest=4: 0x00d50084" 2 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "descriptor-word dest=5: 0x53000000" 2 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "descriptor-word dest=8: 0x00000020" 2 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner verify: ok" 2 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner formed: rows=8" 2 "rvtt_macro_planner" } }
// Preheader per function: the ambient enable, the owned SETC16
// address-modifier program, five descriptor words through the owned
// LREG; body: sixteen launch words (two per row, alternating VDs on
// the value carrier, the sacrificial store-only VD); derived drain 3.
// { dg-final { scan-assembler-times "SFPENCC\\t3, 10" 2 } }
// { dg-final { scan-assembler-times "SFPCONFIG" 10 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2987524096" 2 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2988572674" 2 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2989817856" 2 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2466308096" 8 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2467356672" 8 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2472591360" 16 } }
// { dg-final { scan-assembler-times "SFPNOP" 6 } }
// { dg-final { scan-assembler-not "SFPSWAP" } }
// { dg-final { scan-assembler-not "SFPSTORE" } }
// { dg-final { scan-assembler-not "SFPLOAD\\t" } }
// { dg-final { scan-assembler-not "TTINCRWC" } }

__attribute__((noinline)) void unary_max_loop ()
{
#pragma GCC unroll 8
  for (int row = 0; row < 32; ++row)
    {
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfppopc (0);
      auto v = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      auto cst = __builtin_rvtt_sfpreadlreg (9);
      auto pair = __builtin_rvtt_sfpswap (cst, v, 1);
      auto r = __builtin_rvtt_sfpselect2 (pair, 1);
      __builtin_rvtt_sfpstore (nullptr, r, 0, 0, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}

__attribute__((noinline)) void unary_min_loop ()
{
#pragma GCC unroll 8
  for (int row = 0; row < 32; ++row)
    {
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfppopc (0);
      auto v = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      auto cst = __builtin_rvtt_sfpreadlreg (9);
      auto pair = __builtin_rvtt_sfpswap (v, cst, 1);
      auto r = __builtin_rvtt_sfpselect2 (pair, 0);
      __builtin_rvtt_sfpstore (nullptr, r, 0, 0, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
