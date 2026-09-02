// MVE kernel-unroll realization fire (item #5 stage 2): the paired
// row's doubled body is ordered by the item-level modulo placement
// (place-II 12, copy offset one II) instead of the greedy interleave,
// realizing the kmin=2 overlap the flat order cannot ride.  Two copy
// webs rotate by placement-slot arithmetic through the item-#7 rename
// service -- the L2-class target arrives through the TEMPORAL tier
// (block-free registers are exhausted; see the composition twin) --
// and the realized steady state commits at II 28, strictly below both
// the greedy candidate (29) and the doubled sequential baseline (36).
// The counted replay-capture shape is untouched: Dst rebase 0->2,
// doubled row step, halved countdown.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap -mtt-tensix-optimize-crossrow-pairing -mtt-tensix-optimize-rename-temporal -mtt-tensix-optimize-mve-expand -fdump-rtl-rvtt_schedule-details" }
// { dg-final { scan-rtl-dump "Crossrow mve-expand model: bb \\d+ items=11 ResMII=11 RecMII=0 cross-edges=0" "rvtt_schedule" } }
// { dg-final { scan-rtl-dump-times "Crossrow mve-expand: rotation rename L\\d -> L\\d at uid=\\d+ in bb \\d+ \\(window \\d+\\.\\.\\d+ mod 24\\)" 2 "rvtt_schedule" } }
// { dg-final { scan-rtl-dump "Crossrow pairing mve-expand committed: bb \\d+ kmin=2 place-II=12 rotation-renames=2 realized II 28" "rvtt_schedule" } }
// { dg-final { scan-rtl-dump "Crossrow pairing: bb \\d+ rows=2 nodes=22 II 36 -> 28" "rvtt_schedule" } }
// { dg-final { scan-rtl-dump "trips=32->16" "rvtt_schedule" } }
// { dg-final { scan-assembler-times "TTINCRWC\t0, 4, 0, 0" 1 } }
// { dg-final { scan-assembler {SFPLOAD\tL[0-7], 2, 0, 7} } }

void mve_fire_row ()
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
