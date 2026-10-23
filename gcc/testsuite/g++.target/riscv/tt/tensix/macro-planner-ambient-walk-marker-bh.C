#include "rtl-lane-reset.h"

// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -fdump-rtl-rvtt_macro_planner-details" }
// Check that the entry-ambient walk steps over raw-LREG markers after CC-inert raw words but not after a raw SFPSETCC.
// { dg-final { scan-rtl-dump-times "Macro-planner formation: entry-ambient all-lanes derived" 2 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner formation-refusal: all-lanes-proof-missing \\(ambient-entry-unproven\\)" 1 "rvtt_macro_planner" } }
// The shared lane proof rejects the genuine raw flag write; metadata cannot
// reset flags. Keep both the refusal and the original raw instruction.
// { dg-final { scan-rtl-dump-times "Macro-planner ambient-walk dirty: flags/ROW_MASK unproved" 1 "rvtt_macro_planner" } }
// { dg-final { scan-assembler-times {\.ttinsn[ \t]+2063597569} 1 } }
// { dg-final { scan-rtl-dump-not "opaque-typed-effects" "rvtt_macro_planner" } }

#define TTI(w) __asm__ __volatile__ (".ttinsn %0" :: "n" (w))

static inline void formable_loop (int base)
{
  // Inherit the caller's actual lane state: resetting here would erase
  // the raw CC writer that the negative case is supposed to test.
#pragma GCC unroll 8
  for (int row = 0; row < 32; ++row)
    {
      auto v = __builtin_rvtt_sfpload (nullptr, base, 0, 0, 0, 7);
      auto cst = __builtin_rvtt_sfpreadlreg (9);
      auto pair = __builtin_rvtt_sfpswap (cst, v, 1);
      auto r = __builtin_rvtt_sfpselect2 (pair, 1);
      __builtin_rvtt_sfpstore (nullptr, r, base, 0, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}

__attribute__((noinline)) void effect_marker_walk_fire ()
{
  RVTT_TEST_LANE_RESET ();
  TTI (0x710a0004);		/* SFPLOADI L0 */
  __builtin_rvtt_sfprawlreg_effect (0, 1);
  TTI (0x91000040);		/* SFPCONFIG dest 4 (LoadMacroConfig) */
  __builtin_rvtt_sfprawlreg_effect (1, 0);
  formable_loop (0);
}

__attribute__((noinline)) void read_marker_walk_fire ()
{
  RVTT_TEST_LANE_RESET ();
  TTI (0x710a0004);		/* SFPLOADI L0 */
  auto l0 = __builtin_rvtt_sfpreadlreg (0);
  __builtin_rvtt_sfpwritelreg (l0, 0);
  formable_loop (32);
}

__attribute__((noinline)) void effect_marker_ccwriter_refuse ()
{
  RVTT_TEST_LANE_RESET ();
  TTI (0x7b000001);		/* SFPSETCC: a real CC writer */
  __builtin_rvtt_sfprawlreg_effect (1, 0);
  formable_loop (64);
}
