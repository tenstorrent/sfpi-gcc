// MVE realization generality twin: the fire row with every value
// renamed and the trip count varied (64) -- no name, count, or
// function identity participates in the admission or the placement;
// the realization commits the identical modulo structure and the
// counted capture shape halves 64 -> 32.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap -mtt-tensix-optimize-crossrow-pairing -mtt-tensix-optimize-rename-temporal -mtt-tensix-optimize-mve-expand -fdump-rtl-rvtt_schedule-details" }
// { dg-final { scan-rtl-dump "Crossrow pairing mve-expand committed: bb \\d+ kmin=2 place-II=\\d+ rotation-renames=\\d+ realized II \\d+" "rvtt_schedule" } }
// { dg-final { scan-rtl-dump "trips=64->32" "rvtt_schedule" } }
// { dg-final { scan-assembler-times "TTINCRWC\t0, 4, 0, 0" 1 } }

void some_other_kernel (int n)
{
  (void) n;
  for (unsigned qq = 0; qq != 64; ++qq)
    {
      auto inp = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      auto keep_a = __builtin_rvtt_sfpmul (inp, inp, 0);
      auto keep_b = __builtin_rvtt_sfpmad (inp, inp, inp, 0);
      auto s1 = __builtin_rvtt_sfpmul (inp, inp, 1);
      auto s2 = __builtin_rvtt_sfpmad (s1, inp, inp, 0);
      auto s3 = __builtin_rvtt_sfpmad (s2, inp, inp, 0);
      auto s4 = __builtin_rvtt_sfpmad (s3, s2, inp, 0);
      auto s5 = __builtin_rvtt_sfpmad (s4, inp, inp, 0);
      auto s6 = __builtin_rvtt_sfpmad (s5, s4, inp, 0);
      auto fin = __builtin_rvtt_sfpmad (s6, keep_a, keep_b, 0);
      __builtin_rvtt_sfpstore (nullptr, fin, 0, 0, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
