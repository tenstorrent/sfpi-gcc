// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-hoist -fdump-rtl-rvtt_replay-details" }
// { dg-final { scan-rtl-dump-times "Hoist profitable: modeled benefit 75 >= 60" 2 "rvtt_replay" } }
// { dg-final { scan-rtl-dump-times "Counted-loop replay payload" 2 "rvtt_replay" } }
// { dg-final { scan-rtl-dump-times "Hoisted no-exec capture" 2 "rvtt_replay" } }

// Two independent minimum-trip fire shapes (varied constants relative to
// the boundary test: different loop counters, same arithmetic).
void lowtrip_one ()
{
  auto a = __builtin_rvtt_sfpreadlreg (0);
  auto b = __builtin_rvtt_sfpreadlreg (1);
  auto c = __builtin_rvtt_sfpreadlreg (2);
  auto d = __builtin_rvtt_sfpreadlreg (3);
  for (unsigned ix = 0; ix != 13; ++ix)
    {
      a = __builtin_rvtt_sfpmul (a, a, 0);
      b = __builtin_rvtt_sfpmul (b, b, 0);
      c = __builtin_rvtt_sfpmul (c, c, 0);
      d = __builtin_rvtt_sfpmul (d, d, 0);
      a = __builtin_rvtt_sfpmul (a, a, 0);
      b = __builtin_rvtt_sfpmul (b, b, 0);
      c = __builtin_rvtt_sfpmul (c, c, 0);
      d = __builtin_rvtt_sfpmul (d, d, 0);
    }
  __builtin_rvtt_sfpwritelreg (a, 0);
  __builtin_rvtt_sfpwritelreg (b, 1);
  __builtin_rvtt_sfpwritelreg (c, 2);
  __builtin_rvtt_sfpwritelreg (d, 3);
}

void lowtrip_two ()
{
  auto p = __builtin_rvtt_sfpreadlreg (4);
  auto q = __builtin_rvtt_sfpreadlreg (5);
  auto r = __builtin_rvtt_sfpreadlreg (6);
  auto t = __builtin_rvtt_sfpreadlreg (7);
  for (unsigned k = 13; k != 0; --k)
    {
      p = __builtin_rvtt_sfpmul (p, p, 0);
      q = __builtin_rvtt_sfpmul (q, q, 0);
      r = __builtin_rvtt_sfpmul (r, r, 0);
      t = __builtin_rvtt_sfpmul (t, t, 0);
      p = __builtin_rvtt_sfpmul (p, p, 0);
      q = __builtin_rvtt_sfpmul (q, q, 0);
      r = __builtin_rvtt_sfpmul (r, r, 0);
      t = __builtin_rvtt_sfpmul (t, t, 0);
    }
  __builtin_rvtt_sfpwritelreg (p, 4);
  __builtin_rvtt_sfpwritelreg (q, 5);
  __builtin_rvtt_sfpwritelreg (r, 6);
  __builtin_rvtt_sfpwritelreg (t, 7);
}
