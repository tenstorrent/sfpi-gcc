// MVE realization default-off identity (item #5 stage 2): without
// -mtt-tensix-optimize-mve-expand the realization arm never runs -- no
// mve-expand line of any kind appears -- and the established pairing
// commits its greedy candidate exactly as before (the corpus byte-gate
// carries the stream identity; this twin pins the dump).
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap -mtt-tensix-optimize-crossrow-pairing -mtt-tensix-optimize-rename-temporal -fdump-rtl-rvtt_schedule-details" }
// { dg-final { scan-rtl-dump-not "Crossrow mve-expand" "rvtt_schedule" } }
// { dg-final { scan-rtl-dump "Crossrow pairing: bb \\d+ rows=2 nodes=22 II 36 -> 29" "rvtt_schedule" } }

void mve_default_off_row ()
{
  for (unsigned r = 0; r != 32; ++r)
    {
      auto x = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      auto v1 = __builtin_rvtt_sfpmul (x, x, 0);
      auto v2 = __builtin_rvtt_sfpmad (x, x, x, 0);
      auto c1 = __builtin_rvtt_sfpmul (x, x, 1);
      auto c2 = __builtin_rvtt_sfpmad (c1, x, x, 0);
      auto c3 = __builtin_rvtt_sfpmad (c2, x, x, 0);
      auto c4 = __builtin_rvtt_sfpmad (c3, c2, x, 0);
      auto c5 = __builtin_rvtt_sfpmad (c4, x, x, 0);
      auto c6 = __builtin_rvtt_sfpmad (c5, c4, x, 0);
      auto res = __builtin_rvtt_sfpmad (c6, v1, v2, 0);
      __builtin_rvtt_sfpstore (nullptr, res, 0, 0, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
