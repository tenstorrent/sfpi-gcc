// Scheduler arsenal: Dst store->load pair motion bait.  The SFPSTORE to
// Dst offset 64 and the SFPLOAD from the SAME offset form a memory RAW
// dependence that is INVISIBLE to a pure-LREG dependence DAG (disjoint
// registers).  The kernel is constructed so a naive latency scheduler
// WOULD hoist the reload: the P-chain before the store carries modeled
// one-slot mad stalls, and the reload (plus its dependent tail) is the
// only independent word available to fill them -- hoisting it above
// the store reads stale Dst data.  The list scheduler must name both
// words as dst-access barriers and keep the order byte-identically.
//
// This twin also guards the FUTURE: if Dst-touching words are ever
// admitted as schedulable nodes (the capture-rotation pool widening
// precedent), admission without a same-address pair proof must keep
// refusing exactly this shape.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap -mtt-tensix-optimize-list-schedule -mno-tt-tensix-optimize-replay -fdump-rtl-rvtt_schedule-details" }
// MEASURED NAMING FACT: the loads (which define an LREG) print
// "dst-access"; the stores carry no def and print "scalar-or-defless"
// (the defless check precedes the dst check).  Sound but imprecise;
// both accepted below, finding recorded in the arsenal verdicts.
// [post-scheduler-hardening adjudication] Expectations updated from the
// stage-1 measurements: (a) effect classification now precedes the
// defless check, so Dst words all name dst-access; (b) identical chain
// shapes now DEFER by name to replay capture formation (DU-S4
// repeated-row rule) instead of engaging and refusing no-decrease --
// no motion either way, and the deferral is the stronger contract.
// { dg-final { scan-rtl-dump-times "List-schedule barrier: dst-access uid=\\d+" 4 "rvtt_schedule" } }
// { dg-final { scan-rtl-dump-times "List-schedule barrier: (?:dst-access|scalar-or-defless) uid=\\d+" 4 "rvtt_schedule" } }
// { dg-final { scan-rtl-dump-times "List-schedule deferred: repeated-row shape at uid=\\d+" 2 "rvtt_schedule" } }
// { dg-final { scan-rtl-dump-not "List-schedule: bb" "rvtt_schedule" } }

using vec_t = decltype (__builtin_rvtt_sfpreadlreg (0));

void dst_pair_bait ()
{
  vec_t x = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
  vec_t p = __builtin_rvtt_sfpmad (x, x, x, 0);
  p = __builtin_rvtt_sfpmad (p, x, p, 0);
  p = __builtin_rvtt_sfpmad (p, x, p, 0);
  __builtin_rvtt_sfpstore (nullptr, p, 64, 0, 0, 0, 7);
  /* Same-offset reload: memory RAW through Dst[64], no LREG edge.  */
  vec_t r = __builtin_rvtt_sfpload (nullptr, 64, 0, 0, 0, 7);
  vec_t s = __builtin_rvtt_sfpmad (r, x, r, 0);
  s = __builtin_rvtt_sfpmad (s, x, s, 0);
  s = __builtin_rvtt_sfpmad (s, x, s, 0);
  __builtin_rvtt_sfpstore (nullptr, s, 128, 0, 0, 0, 7);
}
