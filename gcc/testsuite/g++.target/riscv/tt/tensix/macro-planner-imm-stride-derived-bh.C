// Immediate-delta row admission (lane IS, owner-ratified F1 honest fix,
// 2026-08-29): the loop-fusion passes carry part of the per-row Dst
// advance in the address immediates (rows alternate base/base+2 with a
// shared +4 separator) -- previously this fragmented discovery
// (row-not-isomorphic) even though the region is the SAME uniform
// +2-per-row progression the separator-carried shape expresses.  Rows
// equal to rows[0] up to ONE common typed Dst-address delta are
// admitted; finalize_region proves the absolute progression (row k's
// accumulated separator advance plus its immediate delta == k * S, and
// the total separator advance == rows * S); formation mandates the
// absorbed-stride calendar and normalizes every explicit copy back to
// rows[0]'s base.  The emitted calendar is WORD-IDENTICAL to the
// unfused separator-carried form's (proven by count below), so the
// downstream counter state is reproduced exactly.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump-times "Macro-planner region: rows=8 row-len=3 runs=1 stride=2 \\(imm\\) loop=yes" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner formed: rows=8 runs=1 config=preheader lane-proof=materialized-enable" 1 "rvtt_macro_planner" } }
// The fused fire fn's separators are all absorbed and its rows all
// launch-delivered: the identical calendar the unfused shape gets.
// { dg-final { scan-assembler-times "\\.ttinsn\\t2466308096" 4 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2467356672" 4 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2472591360" 8 } }
// { dg-final { scan-assembler-times "SFPENCC\\t3, 10" 1 } }

// NEAR-MISS 1: a non-uniform immediate progression (the second pair
// jumps +6) can never fold into one absorbed stride -- the region
// refuses by the stride-mismatch name and keeps its explicit bytes.
// NEAR-MISS 2: a fused progression whose total separator advance is
// short (final +4 separator missing: the counter the absorbed calendar
// would leave differs from the original's) refuses the same way.
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
  for (int t = 0; t < 4; ++t)
    {
      ROWPAIR (0, 2);
      ROWPAIR (0, 2);
      ROWPAIR (0, 2);
      /* Final pair with NO trailing separator: total separator advance
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
