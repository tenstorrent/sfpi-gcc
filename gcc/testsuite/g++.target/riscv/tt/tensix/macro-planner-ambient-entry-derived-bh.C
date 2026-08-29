// Entry-ambient all-lanes derivation (lane IS, owner-ratified F1 honest
// fix, 2026-08-29): a marker-free loop-body region -- NO pushc/popc
// pair, NO typed enable anywhere in the function -- forms when the
// kill-aware backwards walk proves the configuration placement point
// carries the architectural fn-entry all-lanes ambient state, and the
// formation SYNTHESIZES the canonical all-lanes SFPENCC at the prefix
// head (the same capability-table word the crosscall init hoist already
// synthesizes caller-side).  Two fire functions (renamed + varied
// address) prove name/address independence; the derivation keys on the
// derived lane-state fact alone.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump-times "Macro-planner formation: entry-ambient all-lanes derived" 2 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner formed: rows=8 runs=1 config=preheader lane-proof=materialized-enable" 2 "rvtt_macro_planner" } }
// (verify prints per proven candidate schedule: two per formed fn here)
// { dg-final { scan-rtl-dump-times "Macro-planner verify: ok" 4 "rvtt_macro_planner" } }
// The synthesized enable is the word-exact architectural all-lanes
// SFPENCC, once per fire function's preheader.
// { dg-final { scan-assembler-times "SFPENCC\\t3, 10" 2 } }

// NEAR-MISS 1 (ambient-entry-unproven): a lanes-off pure CC write ahead
// of the loop -- not the trailing preheader instruction (a Dst store
// follows it), so the trailing-enable path finds nothing and the
// ambient walk must decide -- DIRTIES the walk; the formation keeps the
// named refusal and the explicit bytes byte-identically.
// NEAR-MISS 2 (fail-closed calls): an opaque call between the function
// entry and the loop dirties the walk -- the RTL derivation carries no
// TU raw-boundary audit, so calls never pass as CC-transparent here.
// { dg-final { scan-rtl-dump-times "Macro-planner formation-refusal: all-lanes-proof-missing \\(ambient-entry-unproven\\)" 2 "rvtt_macro_planner" } }
// The two near-miss loops keep their explicit rows: 16 swaps, and the
// lanes-off enable word survives untouched.
// (the builtin's positional expansion prints "imm12, mod1" = "10, 0")
// { dg-final { scan-assembler-times "SFPENCC\\t10, 0" 1 } }
// { dg-final { scan-assembler "SFPSWAP" } }

__attribute__((noinline)) void zephyr_ambient_fire ()
{
#pragma GCC unroll 8
  for (int row = 0; row < 32; ++row)
    {
      auto v = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      auto cst = __builtin_rvtt_sfpreadlreg (9);
      auto pair = __builtin_rvtt_sfpswap (cst, v, 1);
      auto r = __builtin_rvtt_sfpselect2 (pair, 1);
      __builtin_rvtt_sfpstore (nullptr, r, 0, 0, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}

__attribute__((noinline)) void quill_ambient_fire_varied ()
{
#pragma GCC unroll 8
  for (int row = 0; row < 32; ++row)
    {
      auto v = __builtin_rvtt_sfpload (nullptr, 32, 0, 0, 0, 7);
      auto cst = __builtin_rvtt_sfpreadlreg (9);
      auto pair = __builtin_rvtt_sfpswap (cst, v, 1);
      auto r = __builtin_rvtt_sfpselect2 (pair, 1);
      __builtin_rvtt_sfpstore (nullptr, r, 96, 0, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}

__attribute__((noinline)) void gale_ambient_lanesoff_refuse ()
{
  /* Lanes-off pure CC write, then a Dst store so the trailing-enable
     probe sees a non-CC tensix word and defers to the ambient walk.  */
  __builtin_rvtt_sfpencc (0, 10);
  auto seed = __builtin_rvtt_sfpload (nullptr, 64, 0, 0, 0, 7);
  __builtin_rvtt_sfpstore (nullptr, seed, 64, 0, 0, 0, 7);
#pragma GCC unroll 8
  for (int row = 0; row < 32; ++row)
    {
      auto v = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      auto cst = __builtin_rvtt_sfpreadlreg (9);
      auto pair = __builtin_rvtt_sfpswap (cst, v, 1);
      auto r = __builtin_rvtt_sfpselect2 (pair, 1);
      __builtin_rvtt_sfpstore (nullptr, r, 0, 0, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}

extern void opaque_kernel_call ();

__attribute__((noinline)) void brook_ambient_call_refuse ()
{
  opaque_kernel_call ();
#pragma GCC unroll 8
  for (int row = 0; row < 32; ++row)
    {
      auto v = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      auto cst = __builtin_rvtt_sfpreadlreg (9);
      auto pair = __builtin_rvtt_sfpswap (cst, v, 1);
      auto r = __builtin_rvtt_sfpselect2 (pair, 1);
      __builtin_rvtt_sfpstore (nullptr, r, 0, 0, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
