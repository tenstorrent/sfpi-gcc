#include "rtl-lane-reset.h"

// Derive full physical lanes from an explicit entry prologue, not an ABI;
// marker-free loop payloads still get a planner-owned preheader enable.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump-times "Macro-planner formation: entry-ambient all-lanes derived" 2 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner formed: rows=8 runs=1 config=preheader lane-proof=materialized-enable" 2 "rvtt_macro_planner" } }
// (verify prints one line per proven candidate schedule, two per function)
// { dg-final { scan-rtl-dump-times "Macro-planner verify: ok" 4 "rvtt_macro_planner" } }
// Four fixture resets plus two fire functions' synthesized preheader enables.
// { dg-final { scan-assembler-times "SFPENCC\\t3, 10" 6 } }

// Near misses: a lanes-off CC write before the loop, and an opaque call
// between function entry and the loop.
// Lanes-off fails flags; an unknown call also loses physical ROW_MASK proof.
// { dg-final { scan-rtl-dump-times "Macro-planner formation-refusal: all-lanes-proof-missing \\(ambient-entry-unproven\\)" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner formation-refusal: cc-enable-unproved \\(ROW_MASK at configuration\\)" 1 "rvtt_macro_planner" } }
// The near-miss loops keep their explicit rows and the lanes-off enable
// ("imm12, mod1" = "0, 10").
// { dg-final { scan-assembler-times "SFPENCC\\t0, 10" 1 } }
// { dg-final { scan-assembler "SFPSWAP" } }

__attribute__((noinline)) void zephyr_ambient_fire ()
{
  RVTT_TEST_LANE_RESET ();
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
  RVTT_TEST_LANE_RESET ();
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
  RVTT_TEST_LANE_RESET ();
  /* Lanes-off pure CC write, then a Dst store so the trailing-enable
     probe sees a non-CC tensix word and defers to the ambient walk.  */
  __builtin_rvtt_sfpencc (10, 0);
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
  RVTT_TEST_LANE_RESET ();
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
