// The seed sub-flag is Blackhole-only exactly like the pairing it
// extends: on Wormhole the pairing refuses by name before any seed
// machinery can run, and the single row is kept byte-identically.
// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap -mtt-tensix-optimize-crossrow-pairing -mtt-tensix-optimize-crossrow-pairing-seed -fdump-rtl-rvtt_schedule-details" }
// { dg-final { scan-rtl-dump "crossrow-pairing-bh-only" "rvtt_schedule" } }
// { dg-final { scan-rtl-dump-not "Crossrow pairing seed" "rvtt_schedule" } }
// { dg-final { scan-rtl-dump-not "Crossrow pairing: bb" "rvtt_schedule" } }
// { dg-final { scan-assembler-times "TTINCRWC\t0, 2, 0, 0" 1 } }
// { dg-final { scan-assembler-not "TTINCRWC\t0, 4, 0, 0" } }

void full_lane_root_row ()
{
  for (unsigned r = 0; r != 32; ++r)
    {
      auto x = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 3);
      auto e = __builtin_rvtt_sfpexexp (x, 0);
      auto b = __builtin_rvtt_sfpmul (x, x, 0);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc_v (e, 0);
      auto c = __builtin_rvtt_sfpassign_lv (x, x);
      c = __builtin_rvtt_sfpassign_lv (c, b);
      __builtin_rvtt_sfppopc (0);
      c = __builtin_rvtt_sfpmad (c, x, x, 0);
      c = __builtin_rvtt_sfpmad (c, b, x, 0);
      c = __builtin_rvtt_sfpmad (c, x, x, 0);
      __builtin_rvtt_sfpstore (nullptr, c, 0, 0, 0, 0, 3);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
