// Check that a body whose doubled LREG pressure exceeds 8 refuses by name (round-interleave-pressure-exceeded).
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap -mtt-tensix-optimize-round-interleave -mno-tt-tensix-optimize-replay -fdump-tree-rvtt_round_interleave -fdump-rtl-rvtt_schedule-details" }
// { dg-final { scan-tree-dump "doubled-body bound \\d+\\+\\d+ exceeds the 8-LREG file" "rvtt_round_interleave" } }
// { dg-final { scan-tree-dump "refused \\(round-interleave-pressure-exceeded\\)" "rvtt_round_interleave" } }
// { dg-final { scan-tree-dump-not "requested unroll" "rvtt_round_interleave" } }
// { dg-final { scan-rtl-dump-not "round-interleave cyclic" "rvtt_schedule" } }

void wide_rounds ()
{
  auto x  = __builtin_rvtt_sfpreadlreg (0);
  auto c1 = __builtin_rvtt_sfpreadlreg (1);
  auto c2 = __builtin_rvtt_sfpreadlreg (3);
  auto c3 = __builtin_rvtt_sfpreadlreg (4);
  auto c4 = __builtin_rvtt_sfpreadlreg (5);
  auto acc = __builtin_rvtt_sfpreadlreg (2);
  for (unsigned r = 0; r != 32; ++r)
    {
      auto t1 = __builtin_rvtt_sfpmad (x, c1, c2, 0);
      auto t2 = __builtin_rvtt_sfpmad (t1, c3, c4, 0);
      auto t3 = __builtin_rvtt_sfpmul (t2, x, 0);
      acc = __builtin_rvtt_sfpand (acc, t3);
    }
  __builtin_rvtt_sfpwritelreg (acc, 2);
}
