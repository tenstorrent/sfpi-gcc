// PRESSURE refusal, by name: the doubled-body union bound
// peak(one body) + peak(body-defined values) must fit the 8-LREG file.
// Five loop-invariant vectors + the reduction carrier + a three-deep
// chain put one body's peak at 8 with 3 body-private values live at
// its widest point: the interleaved bound exceeds 8, so the request
// refuses round-interleave-pressure-exceeded (the honest answer until
// the pre-RA pressure-scheduling gate lands -- the measured lcm/gcd
// two-chain interleave, needing 10 live, is exactly this class).
// Refusal = byte-identical: no annotation, no unroll.
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
