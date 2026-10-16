// Check that the list scheduler does not move instructions across a SETCC..ENCC lane-enable region.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap -mtt-tensix-optimize-list-schedule -mno-tt-tensix-optimize-replay -fdump-rtl-rvtt_schedule-details" }
// SFPSETCC/SFPENCC have no LREG def, so the barrier may print as either class.
// { dg-final { scan-rtl-dump-times "List-schedule barrier: (?:cc-write|scalar-or-defless) uid=\\d+" 2 "rvtt_schedule" } }
// { dg-final { scan-rtl-dump-times "List-schedule deferred: repeated-row shape at uid=\\d+" 3 "rvtt_schedule" } }
// { dg-final { scan-rtl-dump-not "List-schedule: bb" "rvtt_schedule" } }

void cc_region_row ()
{
  auto x  = __builtin_rvtt_sfpreadlreg (0);
  auto p  = __builtin_rvtt_sfpreadlreg (1);
  auto q  = __builtin_rvtt_sfpreadlreg (2);
  auto b  = __builtin_rvtt_sfpreadlreg (3);
  auto r  = __builtin_rvtt_sfpreadlreg (4);
  /* Serial P-chain: one modeled stall per adjacency.  */
  p = __builtin_rvtt_sfpmad (p, x, p, 0);
  p = __builtin_rvtt_sfpmad (p, x, p, 0);
  p = __builtin_rvtt_sfpmad (p, x, p, 0);
  __builtin_rvtt_sfppushc (0);
  __builtin_rvtt_sfpsetcc (b, 0);
  /* Lane-predicated region, independent of P by LREG values.  */
  q = __builtin_rvtt_sfpmad (q, x, q, 0);
  q = __builtin_rvtt_sfpmad (q, x, q, 0);
  q = __builtin_rvtt_sfpmad (q, x, q, 0);
  __builtin_rvtt_sfppopc (0);
  /* Post-region serial tail.  */
  r = __builtin_rvtt_sfpmad (r, x, r, 0);
  r = __builtin_rvtt_sfpmad (r, x, r, 0);
  r = __builtin_rvtt_sfpmad (r, x, r, 0);
  __builtin_rvtt_sfpwritelreg (p, 1);
  __builtin_rvtt_sfpwritelreg (q, 2);
  __builtin_rvtt_sfpwritelreg (r, 4);
}
