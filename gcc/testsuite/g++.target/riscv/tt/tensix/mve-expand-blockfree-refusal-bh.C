// MVE realization composition doorway (item #5 stage 2): the identical
// fire-twin row WITHOUT -mtt-tensix-optimize-rename-temporal -- the
// rotation targets need registers the block-free tier cannot supply
// (every LREG is touched block-wide), so the slot-arithmetic
// assignment finds no service-provable target, the realized order ties
// the greedy candidate, and the realization refuses by name while the
// established pairing proceeds untouched (II 36 -> 29).  The temporal
// tier is exactly what converts this row into the fire twin.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap -mtt-tensix-optimize-crossrow-pairing -mtt-tensix-optimize-mve-expand -fdump-rtl-rvtt_schedule-details" }
// { dg-final { scan-rtl-dump "Crossrow mve-expand refused: mve-expand-no-ii-decrease in bb \\d+ \\(realized 29 vs greedy 29, base 36\\)" "rvtt_schedule" } }
// { dg-final { scan-rtl-dump-not "mve-expand committed" "rvtt_schedule" } }
// { dg-final { scan-rtl-dump "Crossrow pairing: bb \\d+ rows=2 nodes=22 II 36 -> 29" "rvtt_schedule" } }

void mve_blockfree_row ()
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
