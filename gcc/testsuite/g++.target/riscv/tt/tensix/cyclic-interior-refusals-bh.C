// Check the cyclic-interior refusals: backedge-seam, no-ii-decrease, repeated-shape and
// opaque-word.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap -mtt-tensix-optimize-cyclic-region-schedule -mno-tt-tensix-optimize-replay -fdump-rtl-rvtt_schedule-details" }
// { dg-final { scan-rtl-dump "cyclic-interior-backedge-seam at uid=\\d+ in bb \\d+" "rvtt_schedule" } }
// { dg-final { scan-rtl-dump "cyclic-interior-no-ii-decrease at uid=\\d+ in bb \\d+ \\(\\d+ -> \\d+\\)" "rvtt_schedule" } }
// { dg-final { scan-rtl-dump-times "cyclic-interior-repeated-shape at uid=\\d+" 2 "rvtt_schedule" } }
// { dg-final { scan-rtl-dump "cyclic-interior-opaque-word uid=\\d+ in bb \\d+" "rvtt_schedule" } }
// { dg-final { scan-rtl-dump-not "List-schedule \\(cyclic-interior\\): bb" "rvtt_schedule" } }

void cis_seam ()
{
  auto x   = __builtin_rvtt_sfpreadlreg (0);
  auto acc = __builtin_rvtt_sfpreadlreg (2);
  for (unsigned r = 0; r != 32; ++r)
    {
      auto t1 = __builtin_rvtt_sfpmul (x, x, 0);
      auto t2 = __builtin_rvtt_sfpmul (t1, t1, 0);
      auto u1 = __builtin_rvtt_sfpmad (x, x, x, 0);
      acc = __builtin_rvtt_sfpand (acc, __builtin_rvtt_sfpmad (t2, u1, x, 0));
    }
  __builtin_rvtt_sfpwritelreg (acc, 2);
}

void cis_noimprove ()
{
  for (int d = 0; d < 32; ++d)
    {
      auto v = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc (v, 0);
      auto t1 = __builtin_rvtt_sfpmul (v, v, 0);
      auto t2 = __builtin_rvtt_sfpmul (t1, t1, 0);
      auto t3 = __builtin_rvtt_sfpmul (t2, t2, 0);
      auto t4 = __builtin_rvtt_sfpmad (t3, t3, v, 0);
      __builtin_rvtt_sfppopc (0);
      __builtin_rvtt_sfpstore (nullptr, t4, 0, 0, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}

void cis_repeated ()
{
  for (int d = 0; d < 32; ++d)
    {
      auto v = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc (v, 0);
      auto a1 = __builtin_rvtt_sfpmul (v, v, 0);
      auto a2 = __builtin_rvtt_sfpmul (a1, a1, 0);
      auto a3 = __builtin_rvtt_sfpmad (a2, v, v, 0);
      __builtin_rvtt_sfpcompc ();
      auto b1 = __builtin_rvtt_sfpmul (v, v, 0);
      auto b2 = __builtin_rvtt_sfpmul (b1, b1, 0);
      auto b3 = __builtin_rvtt_sfpmad (b2, v, v, 0);
      __builtin_rvtt_sfppopc (0);
      auto w = __builtin_rvtt_sfpxor (a3, b3);
      __builtin_rvtt_sfpstore (nullptr, w, 0, 0, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}

void cis_opaque ()
{
  for (int d = 0; d < 32; ++d)
    {
      auto v = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc (v, 0);
      auto t1 = __builtin_rvtt_sfpmul (v, v, 0);
      auto t2 = __builtin_rvtt_sfpmul (t1, t1, 0);
      auto u1 = __builtin_rvtt_sfpmad (v, v, v, 0);
      auto w  = __builtin_rvtt_sfpmad (t2, u1, v, 0);
      asm volatile (".ttinsn 2149580800");	/* raw SFPNOP-class word */
      __builtin_rvtt_sfppopc (0);
      __builtin_rvtt_sfpstore (nullptr, w, 0, 0, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
