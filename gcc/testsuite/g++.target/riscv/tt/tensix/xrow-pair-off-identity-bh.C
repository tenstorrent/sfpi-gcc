// Flag off (the Init(0) default): the machinery never runs -- no pairing
// line, single row, original separator and trip count.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap -fdump-rtl-rvtt_schedule-details" }
// { dg-final { scan-rtl-dump-not "Crossrow pairing" "rvtt_schedule" } }
// { dg-final { scan-assembler-times "TTINCRWC\t0, 2, 0, 0" 1 } }
// { dg-final { scan-assembler-not "TTINCRWC\t0, 4, 0, 0" } }

void off_row ()
{
  for (unsigned r = 0; r != 32; ++r)
    {
      auto x = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      auto a = __builtin_rvtt_sfpabs (x, 1);
      auto b = __builtin_rvtt_sfpmul (a, x, 0);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc_v (b, 0);
      a = __builtin_rvtt_sfpadd (a, x, 0);
      __builtin_rvtt_sfppopc (0);
      b = __builtin_rvtt_sfpmad (a, b, x, 0);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc_v (b, 0);
      a = __builtin_rvtt_sfpxor (a, b);
      __builtin_rvtt_sfppopc (0);
      __builtin_rvtt_sfpstore (nullptr, a, 0, 0, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
