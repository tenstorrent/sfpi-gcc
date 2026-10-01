// MVE realization demand refusal (modulo variable expansion): four end-read
// long-lived values ride the fire row's chain, so every fitting-II
// placement carries ten simultaneously-live value copies -- more than the 8-LREG file
// net of loop-live invariants can ever hold -- so the expansion
// refuses by stage 1's own name at the realization seam; the greedy
// candidate then refuses on its own terms (no modeled decrease) and
// the single row is kept byte-identically.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap -mtt-tensix-optimize-crossrow-pairing -mtt-tensix-optimize-rename-temporal -mtt-tensix-optimize-mve-expand -fdump-rtl-rvtt_schedule-details" }
// { dg-final { scan-rtl-dump "Crossrow mve-expand refused: mve-rename-exhausted in bb \\d+ \\(kmin=2 demand=10 capacity=8 invariants=0 at place-II=14\\)" "rvtt_schedule" } }
// { dg-final { scan-rtl-dump-not "mve-expand committed" "rvtt_schedule" } }
// { dg-final { scan-rtl-dump "Crossrow pairing refused: no modeled steady-state II decrease in bb \\d+ \\(44 -> 44\\)" "rvtt_schedule" } }

void mve_demand_row ()
{
  for (unsigned r = 0; r != 32; ++r)
    {
      auto x = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      auto v1 = __builtin_rvtt_sfpmul (x, x, 0);
      auto v2 = __builtin_rvtt_sfpmad (x, x, x, 0);
      auto v3 = __builtin_rvtt_sfpabs (x, 1);
      auto v4 = __builtin_rvtt_sfpadd (x, x, 0);
      auto c1 = __builtin_rvtt_sfpmul (x, x, 1);
      auto c2 = __builtin_rvtt_sfpmad (c1, x, x, 0);
      auto c3 = __builtin_rvtt_sfpmad (c2, x, x, 0);
      auto c4 = __builtin_rvtt_sfpmad (c3, c2, x, 0);
      auto c5 = __builtin_rvtt_sfpmad (c4, x, x, 0);
      auto c6 = __builtin_rvtt_sfpmad (c5, c4, x, 0);
      auto r1 = __builtin_rvtt_sfpmad (c6, v1, v2, 0);
      auto r2 = __builtin_rvtt_sfpmad (r1, v3, v4, 0);
      __builtin_rvtt_sfpstore (nullptr, r2, 0, 0, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
