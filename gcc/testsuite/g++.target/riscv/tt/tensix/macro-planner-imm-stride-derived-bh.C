#include "rtl-lane-reset.h"

// Check that rows differing only by a common Dst-address immediate delta form the absorbed-stride calendar.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump-times "Macro-planner region: rows=8 row-len=3 runs=1 stride=2 \\(imm\\) loop=yes" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner formed: rows=8 runs=1 config=preheader lane-proof=materialized-enable" 1 "rvtt_macro_planner" } }
// The fused rows absorb every separator and are all launch-delivered.
// { dg-final { scan-assembler-times "\\.ttinsn\\t2466308096" 4 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2467356672" 4 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2472591360" 8 } }
// Three fixture lane resets and the one formed loop's planner enable.
// { dg-final { scan-assembler-times "SFPENCC\\t3, 10" 4 } }

// A non-uniform immediate progression (the second pair jumps +6) and a
// progression whose total separator advance is short both refuse with
// row-stride-mismatch.
// { dg-final { scan-rtl-dump-times "Macro-planner refusal: row-stride-mismatch" 2 "rvtt_macro_planner" } }
// The near-miss loops keep explicit swaps and separators.
// { dg-final { scan-assembler "SFPSWAP" } }
// { dg-final { scan-assembler "TTINCRWC" } }

#define ROWPAIR(base, step)                                                  \
  do                                                                         \
    {                                                                        \
      auto a0 = __builtin_rvtt_sfpload (nullptr, base, 0, 0, 0, 7);          \
      auto c0 = __builtin_rvtt_sfpreadlreg (9);                              \
      auto p0 = __builtin_rvtt_sfpswap (c0, a0, 1);                          \
      auto r0 = __builtin_rvtt_sfpselect2 (p0, 1);                           \
      __builtin_rvtt_sfpstore (nullptr, r0, base, 0, 0, 0, 7);               \
      auto a1 = __builtin_rvtt_sfpload (nullptr, (base) + (step), 0, 0, 0, 7); \
      auto c1 = __builtin_rvtt_sfpreadlreg (9);                              \
      auto p1 = __builtin_rvtt_sfpswap (c1, a1, 1);                          \
      auto r1 = __builtin_rvtt_sfpselect2 (p1, 1);                           \
      __builtin_rvtt_sfpstore (nullptr, r1, (base) + (step), 0, 0, 0, 7);    \
      __builtin_rvtt_ttincrwc (0, 4, 0, 0);                                  \
    }                                                                        \
  while (0)

__attribute__((noinline)) void fused_imm_fire ()
{
  RVTT_TEST_LANE_RESET ();
  for (int t = 0; t < 4; ++t)
    {
      ROWPAIR (0, 2);
      ROWPAIR (0, 2);
      ROWPAIR (0, 2);
      ROWPAIR (0, 2);
    }
}

__attribute__((noinline)) void fused_imm_nonuniform_refuse ()
{
  RVTT_TEST_LANE_RESET ();
  for (int t = 0; t < 4; ++t)
    {
      ROWPAIR (0, 2);
      ROWPAIR (0, 6);
      ROWPAIR (0, 2);
      ROWPAIR (0, 2);
    }
}

__attribute__((noinline)) void fused_imm_short_counter_refuse ()
{
  RVTT_TEST_LANE_RESET ();
  for (int t = 0; t < 4; ++t)
    {
      ROWPAIR (0, 2);
      ROWPAIR (0, 2);
      ROWPAIR (0, 2);
      /* Final pair with no trailing separator: total separator advance
	 12 != 8 * 2.  */
      auto a0 = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      auto c0 = __builtin_rvtt_sfpreadlreg (9);
      auto p0 = __builtin_rvtt_sfpswap (c0, a0, 1);
      auto r0 = __builtin_rvtt_sfpselect2 (p0, 1);
      __builtin_rvtt_sfpstore (nullptr, r0, 0, 0, 0, 0, 7);
      auto a1 = __builtin_rvtt_sfpload (nullptr, 2, 0, 0, 0, 7);
      auto c1 = __builtin_rvtt_sfpreadlreg (9);
      auto p1 = __builtin_rvtt_sfpswap (c1, a1, 1);
      auto r1 = __builtin_rvtt_sfpselect2 (p1, 1);
      __builtin_rvtt_sfpstore (nullptr, r1, 2, 0, 0, 0, 7);
      /* Short final separator: total separator advance 14 != 8 * 2.  */
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
