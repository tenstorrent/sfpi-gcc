// Keep twin (II gate): an atom-dominant row -- the whole computation is
// one indivisible CC atom -- models no steady-state II decrease when
// paired (atoms serialize), so the pairing refuses and the single row is
// kept byte-identically.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap -mtt-tensix-optimize-crossrow-pairing -fdump-rtl-rvtt_schedule-details" }
// { dg-final { scan-rtl-dump "Crossrow pairing refused: no modeled steady-state II decrease" "rvtt_schedule" } }
// { dg-final { scan-rtl-dump-not "Crossrow pairing: bb" "rvtt_schedule" } }
// { dg-final { scan-assembler-times "TTINCRWC\t0, 2, 0, 0" 1 } }
// { dg-final { scan-assembler-not "TTINCRWC\t0, 4, 0, 0" } }

void atom_dominant_row ()
{
  for (unsigned r = 0; r != 32; ++r)
    {
      auto x = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc_v (x, 0);
      x = __builtin_rvtt_sfpmad (x, x, x, 0);
      x = __builtin_rvtt_sfpmad (x, x, x, 0);
      x = __builtin_rvtt_sfpmad (x, x, x, 0);
      __builtin_rvtt_sfppopc (0);
      __builtin_rvtt_sfpstore (nullptr, x, 0, 0, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
