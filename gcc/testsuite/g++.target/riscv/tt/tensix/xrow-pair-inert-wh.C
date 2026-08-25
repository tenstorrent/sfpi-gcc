// Wormhole: the pairing's latency/adjacency model family is audited on
// Blackhole only -- named refusal, stream untouched.
// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap -mtt-tensix-optimize-crossrow-pairing -fdump-rtl-rvtt_schedule-details" }
// { dg-final { scan-rtl-dump "crossrow-pairing-bh-only" "rvtt_schedule" } }
// { dg-final { scan-rtl-dump-not "Crossrow pairing: bb" "rvtt_schedule" } }
// { dg-final { scan-assembler-times "TTINCRWC\t0, 2, 0, 0" 1 } }

void wh_row ()
{
  for (unsigned r = 0; r != 32; ++r)
    {
      auto x = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 3);
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
      __builtin_rvtt_sfpstore (nullptr, a, 0, 0, 0, 0, 3);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
