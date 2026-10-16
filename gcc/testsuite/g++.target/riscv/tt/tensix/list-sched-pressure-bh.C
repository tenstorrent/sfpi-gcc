// Check that the list scheduler finds a fitting interleave at the eight-LREG boundary and never refuses on pressure.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap -mtt-tensix-optimize-list-schedule -mno-tt-tensix-optimize-replay -fdump-rtl-rvtt_schedule-details" }
// fn 1: 14 words + 7 chain stalls + 1 drain = 22, interleaved 15.  fn 2:
// 23 -> 15, with c1's writeback kept before c8 reuses its register.
// { dg-final { scan-rtl-dump-times "List-schedule: bb \\d+ nodes=14 makespan 22 -> 15 target=bh" 1 "rvtt_schedule" } }
// { dg-final { scan-rtl-dump-times "List-schedule: bb \\d+ nodes=15 makespan 23 -> 15 target=bh" 1 "rvtt_schedule" } }
// { dg-final { scan-rtl-dump-not "List-schedule refused: pressure" "rvtt_schedule" } }

void pressure_fit_7 ()
{
  auto x  = __builtin_rvtt_sfpreadlreg (0);
  auto c1 = __builtin_rvtt_sfpreadlreg (1);
  auto c2 = __builtin_rvtt_sfpreadlreg (2);
  auto c3 = __builtin_rvtt_sfpreadlreg (3);
  auto c4 = __builtin_rvtt_sfpreadlreg (4);
  auto c5 = __builtin_rvtt_sfpreadlreg (5);
  auto c6 = __builtin_rvtt_sfpreadlreg (6);
  auto c7 = __builtin_rvtt_sfpreadlreg (7);
  c1 = __builtin_rvtt_sfpmad (c1, x, c1, 0);
  c1 = __builtin_rvtt_sfpmad (c1, x, c1, 0);
  c2 = __builtin_rvtt_sfpmad (c2, x, c2, 0);
  c2 = __builtin_rvtt_sfpmad (c2, x, c2, 0);
  c3 = __builtin_rvtt_sfpmad (c3, x, c3, 0);
  c3 = __builtin_rvtt_sfpmad (c3, x, c3, 0);
  c4 = __builtin_rvtt_sfpmad (c4, x, c4, 0);
  c4 = __builtin_rvtt_sfpmad (c4, x, c4, 0);
  c5 = __builtin_rvtt_sfpmad (c5, x, c5, 0);
  c5 = __builtin_rvtt_sfpmad (c5, x, c5, 0);
  c6 = __builtin_rvtt_sfpmad (c6, x, c6, 0);
  c6 = __builtin_rvtt_sfpmad (c6, x, c6, 0);
  c7 = __builtin_rvtt_sfpmad (c7, x, c7, 0);
  c7 = __builtin_rvtt_sfpmad (c7, x, c7, 0);
  __builtin_rvtt_sfpwritelreg (c1, 1);
  __builtin_rvtt_sfpwritelreg (c2, 2);
  __builtin_rvtt_sfpwritelreg (c3, 3);
  __builtin_rvtt_sfpwritelreg (c4, 4);
  __builtin_rvtt_sfpwritelreg (c5, 5);
  __builtin_rvtt_sfpwritelreg (c6, 6);
  __builtin_rvtt_sfpwritelreg (c7, 7);
}

void pressure_reuse_8 ()
{
  auto x  = __builtin_rvtt_sfpreadlreg (0);
  auto c1 = __builtin_rvtt_sfpreadlreg (1);
  auto c2 = __builtin_rvtt_sfpreadlreg (2);
  auto c3 = __builtin_rvtt_sfpreadlreg (3);
  auto c4 = __builtin_rvtt_sfpreadlreg (4);
  auto c5 = __builtin_rvtt_sfpreadlreg (5);
  auto c6 = __builtin_rvtt_sfpreadlreg (6);
  auto c7 = __builtin_rvtt_sfpreadlreg (7);
  c1 = __builtin_rvtt_sfpmad (c1, x, c1, 0);
  c1 = __builtin_rvtt_sfpmad (c1, x, c1, 0);
  __builtin_rvtt_sfpwritelreg (c1, 1);
  /* Chain 8's accumulator must reuse a register freed by an earlier
     chain's writeback.  */
  auto c8 = __builtin_rvtt_sfpmad (x, x, x, 0);
  c8 = __builtin_rvtt_sfpmad (c8, x, c8, 0);
  c2 = __builtin_rvtt_sfpmad (c2, x, c2, 0);
  c2 = __builtin_rvtt_sfpmad (c2, x, c2, 0);
  c3 = __builtin_rvtt_sfpmad (c3, x, c3, 0);
  c3 = __builtin_rvtt_sfpmad (c3, x, c3, 0);
  c4 = __builtin_rvtt_sfpmad (c4, x, c4, 0);
  c4 = __builtin_rvtt_sfpmad (c4, x, c4, 0);
  c5 = __builtin_rvtt_sfpmad (c5, x, c5, 0);
  c5 = __builtin_rvtt_sfpmad (c5, x, c5, 0);
  c6 = __builtin_rvtt_sfpmad (c6, x, c6, 0);
  c6 = __builtin_rvtt_sfpmad (c6, x, c6, 0);
  c7 = __builtin_rvtt_sfpmad (c7, x, c7, 0);
  c7 = __builtin_rvtt_sfpmad (c7, x, c7, 0);
  __builtin_rvtt_sfpwritelreg (c8, 0);
  __builtin_rvtt_sfpwritelreg (c2, 2);
  __builtin_rvtt_sfpwritelreg (c3, 3);
  __builtin_rvtt_sfpwritelreg (c4, 4);
  __builtin_rvtt_sfpwritelreg (c5, 5);
  __builtin_rvtt_sfpwritelreg (c6, 6);
  __builtin_rvtt_sfpwritelreg (c7, 7);
}
