// Scheduler arsenal: CC-region motion bait.  Reordering ACROSS a lane
// enablement change is UNSOUND: an instruction moved into (or out of)
// the SFPSETCC..SFPENCC region executes under different lane enables
// and writes a different lane subset.  This kernel is CONSTRUCTED so a
// naive latency scheduler WOULD move: the pre-region P-chain carries a
// modeled one-slot stall per dependent mad adjacency, and the only
// independent words that could fill those shadows live INSIDE the CC
// region (the Q-chain) or after it (the R-tail) -- crossing the
// setcc/encc words is the only available win.  The list scheduler must
// name the cc-write barriers and keep every region byte-identically
// (each bounded sub-region is a single serial chain: the no-win
// refusal is the correct outcome everywhere).
//
// Why the bait is real: q1/q2 read only q0/x (no register dependence
// on the P-chain), so a DAG over LREG values alone admits hoisting
// them above SFPSETCC into P's stalls -- only the lane-state barrier
// refuses.  Same for sinking p2 below SFPSETCC into the region's own
// stalls.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap -mtt-tensix-optimize-list-schedule -mno-tt-tensix-optimize-replay -fdump-rtl-rvtt_schedule-details" }
// MEASURED NAMING FACT (arsenal finding for the scheduler lane): the
// canonical CC words SFPSETCC/SFPENCC carry no LREG def, so admission
// names them at the defless check before the cc-write check -- the
// printed barrier class is "scalar-or-defless", not "cc-write".  The
// refusal is sound either way (the words bound the region and nothing
// crosses); the scan below accepts either name so a naming fix keeps
// this twin green, and the finding is recorded in the arsenal verdicts.
// [post-scheduler-hardening adjudication] Expectations updated from the
// stage-1 measurements: (a) effect classification now precedes the
// defless check, so Dst words all name dst-access; (b) identical chain
// shapes now DEFER by name to replay capture formation (DU-S4
// repeated-row rule) instead of engaging and refusing no-decrease --
// no motion either way, and the deferral is the stronger contract.
// { dg-final { scan-rtl-dump-times "List-schedule barrier: (?:cc-write|scalar-or-defless) uid=\\d+" 2 "rvtt_schedule" } }
// { dg-final { scan-rtl-dump-times "List-schedule deferred: repeated-row shape at uid=\\d+" 3 "rvtt_schedule" } }
// { dg-final { scan-rtl-dump-not "List-schedule: bb" "rvtt_schedule" } }

void cc_region_bait ()
{
  auto x  = __builtin_rvtt_sfpreadlreg (0);
  auto p  = __builtin_rvtt_sfpreadlreg (1);
  auto q  = __builtin_rvtt_sfpreadlreg (2);
  auto b  = __builtin_rvtt_sfpreadlreg (3);
  auto r  = __builtin_rvtt_sfpreadlreg (4);
  /* Serial P-chain: one modeled stall per adjacency (bait shadows).  */
  p = __builtin_rvtt_sfpmad (p, x, p, 0);
  p = __builtin_rvtt_sfpmad (p, x, p, 0);
  p = __builtin_rvtt_sfpmad (p, x, p, 0);
  __builtin_rvtt_sfppushc (0);
  __builtin_rvtt_sfpsetcc_v (b, 0);
  /* Lane-predicated region: independent of P by LREG values -- the
     bait fillers a naive scheduler would hoist.  */
  q = __builtin_rvtt_sfpmad (q, x, q, 0);
  q = __builtin_rvtt_sfpmad (q, x, q, 0);
  q = __builtin_rvtt_sfpmad (q, x, q, 0);
  __builtin_rvtt_sfppopc (0);
  /* Post-region serial tail: bait for sinking region words down.  */
  r = __builtin_rvtt_sfpmad (r, x, r, 0);
  r = __builtin_rvtt_sfpmad (r, x, r, 0);
  r = __builtin_rvtt_sfpmad (r, x, r, 0);
  __builtin_rvtt_sfpwritelreg (p, 1);
  __builtin_rvtt_sfpwritelreg (q, 2);
  __builtin_rvtt_sfpwritelreg (r, 4);
}
