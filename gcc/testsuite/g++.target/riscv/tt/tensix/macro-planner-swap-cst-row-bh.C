// { dg-options "-mcpu=tt-bh-tensix -O3 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -fdump-rtl-rvtt_macro_planner-details" }
// The unary max/min row shape: SFPLOAD / SFPSWAP against a hardware
// constant register / SFPSTORE.  The constant-register swap variants
// are the admitted derived-calendar template class: the merged ii=1
// candidate refuses the SFPSWAP adjacency rule by name; the
// store-demoted candidate DERIVES the full calendar -- swap template
// packed from the admitted source (the result-routing sense follows
// the operand layout: constant-in-VD flips to mod 9, constant-in-VC
// keeps mod 1), the staging copy on the swap's macro (the frozen
// minmax transient-copy realization, re-derived), the sequence words
// 0x00d50084 / 0x53000000 from the schedule, and the field-derived
// misc.  These four-row STRAIGHT-LINE runs then refuse the derived
// profitability gate honestly; formation coverage is the loop-shaped
// twin (macro-planner-derived-unary-maxmin-loop-bh.C).
// { dg-final { scan-rtl-dump-not "row-opaque-effect" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "row-not-closed" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner region: rows=4 row-len=3" 2 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "row-subunits: load,simple,store" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "sequence-derivation-hazard" 2 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "descriptor-word dest=0: 0x920009c9" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "descriptor-word dest=0: 0x920009c1" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "descriptor-word dest=1: 0x940000d6" 2 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "descriptor-word dest=4: 0x00d50084" 2 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "descriptor-word dest=5: 0x53000000" 2 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "descriptor-word dest=8: 0x00000020" 2 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "formation-refusal: unprofitable" 2 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "descriptor-encoding-failed" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-not "\\.ttinsn" } }
// { dg-final { scan-assembler-not "SFPCONFIG" } }
// { dg-final { scan-assembler-times "SFPSWAP" 8 } }

__attribute__((noinline)) void unary_max_rows ()
{
  __builtin_rvtt_sfppushc (0);
  __builtin_rvtt_sfppopc (0);
#pragma GCC unroll 4
  for (int row = 0; row < 4; ++row)
    {
      auto v = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      auto zero = __builtin_rvtt_sfpreadlreg (9);
      auto pair = __builtin_rvtt_sfpswap (zero, v, 1);
      auto r = __builtin_rvtt_sfpselect2 (pair, 1);
      __builtin_rvtt_sfpstore (nullptr, r, 0, 0, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}

__attribute__((noinline)) void unary_min_rows ()
{
  __builtin_rvtt_sfppushc (0);
  __builtin_rvtt_sfppopc (0);
#pragma GCC unroll 4
  for (int row = 0; row < 4; ++row)
    {
      auto v = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      auto zero = __builtin_rvtt_sfpreadlreg (9);
      auto pair = __builtin_rvtt_sfpswap (v, zero, 1);
      auto r = __builtin_rvtt_sfpselect2 (pair, 0);
      __builtin_rvtt_sfpstore (nullptr, r, 0, 0, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
