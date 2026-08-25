// Rename lane-domain discipline: a fresh definition INSIDE a CC atom may
// not root a rename web (a dead LREG would expose stale disabled-lane
// bits) -- the refusal is named; with the webs unrenamed the copies
// serialize on the shared registers in the dependence-legal candidate,
// the modeled II does not improve, and the single row is kept
// byte-identically.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap -mtt-tensix-optimize-crossrow-pairing -fdump-rtl-rvtt_schedule-details" }
// { dg-final { scan-rtl-dump "crossrow-pairing-rename-cc-domain" "rvtt_schedule" } }
// { dg-final { scan-rtl-dump "Crossrow pairing refused: no modeled steady-state II decrease" "rvtt_schedule" } }
// { dg-final { scan-rtl-dump-not "Crossrow pairing: bb" "rvtt_schedule" } }
// { dg-final { scan-assembler-times "TTINCRWC\t0, 2, 0, 0" 1 } }
// { dg-final { scan-assembler-not "TTINCRWC\t0, 4, 0, 0" } }

void predicated_accumulation_row ()
{
  for (unsigned r = 0; r != 32; ++r)
    {
      auto x = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      auto a = __builtin_rvtt_sfpabs (x, 1);
      auto b = __builtin_rvtt_sfpmul (a, x, 0);
      auto c = __builtin_rvtt_sfpadd (a, x, 0);
      auto d = __builtin_rvtt_sfpmad (b, c, x, 0);
      auto e = __builtin_rvtt_sfpmad (a, d, c, 0);
      auto f = __builtin_rvtt_sfpmad (b, e, d, 0);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc_v (f, 0);
      f = __builtin_rvtt_sfpadd (f, a, 0);
      f = __builtin_rvtt_sfpadd (f, b, 0);
      f = __builtin_rvtt_sfpadd (f, c, 0);
      f = __builtin_rvtt_sfpadd (f, d, 0);
      f = __builtin_rvtt_sfpadd (f, e, 0);
      f = __builtin_rvtt_sfpadd (f, x, 0);
      __builtin_rvtt_sfppopc (0);
      __builtin_rvtt_sfpstore (nullptr, f, 0, 0, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
