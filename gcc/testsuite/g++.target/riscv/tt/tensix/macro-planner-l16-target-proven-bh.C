// LReg16-staging opcode guard (honest-modeling fix): an
// event redirected to the LReg16 staging register has no encodable VD
// and executes through the direct template evaluator, which is proven
// only for the oracle's opcode set (rvtt-macro-tables.cc
// opcode_l16_target_proven).  The SFPABS (0x7d) row -- the first shape
// outside the set to reach formation, via the entry-ambient derivation
// -- was adjudicated WRONG on BH hardware under LReg16 staging (a
// absint32 int-abs witness; the oracle refuses it as
// UnsupportedFunctionality).  A VD-direct fallback then targeted the launch
// VD through rewritten-word execution.  Silicon subsequently disproved that
// fallback too: the Blackhole macro evaluator produced alternating zero
// lanes for int32 abs while the explicit lowering was correct.  SFPABS is
// therefore absent from the Blackhole macro execution capability; the
// same-shape SFPCAST control remains admitted.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump "Macro-planner descriptor-refusal: subunit-placement-unproven" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner formed: rows=1 runs=1 config=preheader lane-proof=materialized-enable" 1 "rvtt_macro_planner" } }
// The proven-set CONTROL (SFPCAST 0x90 producer, same row shape) keeps the
// LReg16 staging: sequence word
// 0x4b000044 (template0 and store both vd16-flagged) -- the exact
// discrimination the guard makes.
// { dg-final { scan-assembler-times "SFPABS" 1 } }
// { dg-final { scan-rtl-dump-times "descriptor-word dest=4: 0x4b000044" 1 "rvtt_macro_planner" } }

__attribute__((noinline)) void abs_vd_direct_loop ()
{
  for (int row = 0; row < 32; ++row)
    {
      auto v = __builtin_rvtt_sfpload (nullptr, 0, 4, 0, 0, 7);
      auto r = __builtin_rvtt_sfpabs (v, 0);
      __builtin_rvtt_sfpstore (nullptr, r, 0, 4, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}

__attribute__((noinline)) void cast_l16_control_loop ()
{
  for (int row = 0; row < 32; ++row)
    {
      auto v = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      auto r = __builtin_rvtt_sfpcast (v, 0);
      __builtin_rvtt_sfpstore (nullptr, r, 0, 0, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
