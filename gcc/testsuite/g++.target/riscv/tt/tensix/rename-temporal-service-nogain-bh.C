// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap -mtt-tensix-optimize-cyclic-region-schedule -mtt-tensix-optimize-rename-temporal -mno-tt-tensix-optimize-replay -fdump-rtl-rvtt_schedule-details" }
// The temporal tier self-prices in SERVICE mode: the cyclic-interior
// consumer requests a chain rename for its region's storage collision,
// whole-block-free targets are exhausted (five loop invariants plus the
// row's packed lifetimes occupy the file), and the only admissible
// target is temporally scoped -- so the request must clear the tier's
// strict-gain bar BEFORE the consumer's own row-II acceptance sees it,
// and it cannot (no temporal rename buys modeled slots; the file
// header's pricing note).  The request refuses by name, no interior
// rename commits, and the consumer proceeds without it.  This is the
// exact composition that lost 4.2% kernel cycles on silicon when a
// consumer-priced temporal rename (modeled row-II win of 2) dissolved
// its row's replay window.
// { dg-final { scan-rtl-dump "regrename-temporal-no-modeled-gain" "rvtt_schedule" } }
// { dg-final { scan-rtl-dump-not "List-schedule \\(interior-rename\\): committed" "rvtt_schedule" } }
// { dg-final { scan-rtl-dump-not "Lreg chain rename \\(temporal\\)" "rvtt_schedule" } }
void rent_service_nogain ()
{
  auto k1 = __builtin_rvtt_sfpreadlreg (0);
  auto k2 = __builtin_rvtt_sfpreadlreg (1);
  auto k3 = __builtin_rvtt_sfpreadlreg (3);
  auto k5 = __builtin_rvtt_sfpreadlreg (5);
  auto k6 = __builtin_rvtt_sfpreadlreg (6);
  for (int d = 0; d < 32; ++d)
    {
      auto v = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc_v (v, 0);
      auto t1 = __builtin_rvtt_sfpmul (v, k1, 0);
      auto t2 = __builtin_rvtt_sfpmul (t1, t1, 0);
      auto u1 = __builtin_rvtt_sfpmad (v, k2, v, 0);
      auto w  = __builtin_rvtt_sfpmad (t2, u1, v, 0);
      __builtin_rvtt_sfppopc (0);
      __builtin_rvtt_sfpstore (nullptr, w, 0, 0, 0, 0, 7);
      auto y = __builtin_rvtt_sfpmul (k5, k6, 0);
      __builtin_rvtt_sfpstore (nullptr, y, 0, 64, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
  __builtin_rvtt_sfpwritelreg (k3, 3);
  __builtin_rvtt_sfpwritelreg (k5, 5);
  __builtin_rvtt_sfpwritelreg (k6, 6);
}
