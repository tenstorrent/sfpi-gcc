// The original uniform-mode envelope covered the frozen whole-word signbit
// program; the DERIVED calendar carries the store's own data mode
// in the architectural Misc.StoreMod0 field (SFPLOADMACRO.md; the same
// field-derived route the hardware-validated where select misc uses),
// so the mode-mismatched row now forms through the derived path -- the
// frozen program still refuses it (build_expectations mirrors the
// synthesis-side uniform-mode filter).
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump "Macro-planner descriptor: templates=2 seq=1 misc=0x00000002 setc16=3 launches=1 drain=3" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner formed: rows=1 runs=1 config=preheader" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-not "SFPSHFT" } }
// { dg-final { scan-assembler-not "TTINCRWC" } }

__attribute__((noinline)) void staged_mode_mismatch (unsigned iterations)
{
  for (unsigned row = 0; row < iterations; ++row)
    {
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfppopc (0);
      auto loaded = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      auto shifted = __builtin_rvtt_sfpshft_i (nullptr, loaded, -31, 0, 0, 0);
      auto converted = __builtin_rvtt_sfpcast (shifted, 0);
      __builtin_rvtt_sfpstore (nullptr, converted, 0, 0, 0, 2, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
