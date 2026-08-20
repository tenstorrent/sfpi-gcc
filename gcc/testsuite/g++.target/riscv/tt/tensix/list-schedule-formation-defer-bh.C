// Formation interaction (replay ON): the list scheduler runs BEFORE
// replay capture formation and must not scramble what formation
// consumes.  Two named deferrals prove the discipline:
// - a counted self-loop row is a CYCLE (back-to-back across the
//   backedge and across every playback of a capture): the block defers
//   entirely -- the linear boundary model mispredicts the seam, which
//   is capture rotation's audited territory;
// - unrolled row copies (straight-line repeats of one region shape)
//   defer by name: sibling copies must stay textually isomorphic for
//   the replay former's re-roll, and differing entry/exit contexts
//   would schedule them differently.
// No fire anywhere in this file: the stream reaches formation
// byte-identically.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap -fno-unroll-loops -mtt-tensix-optimize-list-schedule -fdump-rtl-rvtt_schedule-details" }
// { dg-final { scan-rtl-dump "List-schedule deferred: cyclic row adjacency in bb \\d+" "rvtt_schedule" } }
// { dg-final { scan-rtl-dump "List-schedule deferred: repeated-row shape at uid=\\d+" "rvtt_schedule" } }
// { dg-final { scan-rtl-dump-not "List-schedule: bb" "rvtt_schedule" } }

void counted_row_defers ()
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  auto c = __builtin_rvtt_sfpreadlreg (2);
  auto d = __builtin_rvtt_sfpreadlreg (3);
  for (unsigned row = 0; row != 20; ++row)
    {
      auto p = __builtin_rvtt_sfpmul (x, x, 0);
      c = __builtin_rvtt_sfpand (c, c);
      d = __builtin_rvtt_sfpor (d, d);
      x = __builtin_rvtt_sfpmul (p, p, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
  __builtin_rvtt_sfpwritelreg (c, 2);
  __builtin_rvtt_sfpwritelreg (d, 3);
}

void unrolled_copies_defer ()
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  auto p = __builtin_rvtt_sfpreadlreg (1);
  auto q = __builtin_rvtt_sfpreadlreg (2);
  auto b = __builtin_rvtt_sfpreadlreg (3);
  // Copy 1.
  p = __builtin_rvtt_sfpmad (p, x, p, 0);
  p = __builtin_rvtt_sfpmad (p, x, p, 0);
  q = __builtin_rvtt_sfpmad (q, x, q, 0);
  __builtin_rvtt_sfppushc (0);
  __builtin_rvtt_sfpsetcc_v (b, 0);
  __builtin_rvtt_sfppopc (0);
  // Copy 2: the same region shape -- both defer by name.
  p = __builtin_rvtt_sfpmad (p, x, p, 0);
  p = __builtin_rvtt_sfpmad (p, x, p, 0);
  q = __builtin_rvtt_sfpmad (q, x, q, 0);
  __builtin_rvtt_sfpwritelreg (p, 1);
  __builtin_rvtt_sfpwritelreg (q, 2);
}
