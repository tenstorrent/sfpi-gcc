// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-dst-autoincr -fdump-rtl-rvtt_dst_autoincr-details" }
//
// lane FS (FP-3) preservation twin: the sibling-arm widening must NOT refuse
// the LEGITIMATE deliverer of a replay-delivered group -- a no-exec record
// that DOMINATES the group (records once, in a block that runs before every
// launch of the same invocation) and is not re-ingested inside a loop the
// group also lives in.  This is the witnessed-good record-hoist mechanism
// (cf. dst-autoincr-loop-bh).  The record here sits in the entry block,
// dominating the launch/mod-write group, so it admits and the group forms.
//
// { dg-final { scan-rtl-dump "Dst-autoincr group: bb \[0-9\]+ rows \[0-9\]+" "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump-not "mod-write-noexec-record-composition-unaudited" "rvtt_dst_autoincr" } }

using vec_t = __xtt_vector;
volatile unsigned sink;

void
dominating_deliverer (unsigned go)
{
  // No-exec record in the entry block: dominates the group's block below
  // (a separate block, so this is not the fail-closed same-block case, and
  // the group's block is not reachable back to here -- the deliverer shape).
  __builtin_rvtt_ttreplay (nullptr, 4, 0, 0, 0, 0, 1);
  vec_t a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
  vec_t p = __builtin_rvtt_sfpmul (a, a, 0);
  p = __builtin_rvtt_sfpmul (p, a, 0);
  __builtin_rvtt_sfpstore (nullptr, p, 0, 0, 0, 0, 7);
  sink = go;

  if (!go)
    return;

  // Replay-delivered group in a dominated block: launches deliver the
  // recorded store; the entry record dominates every launch.
  __builtin_rvtt_ttreplay (nullptr, 4, 0, 0, 0, 0, 0);
  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
  __builtin_rvtt_ttreplay (nullptr, 4, 0, 0, 0, 0, 0);
  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
  __builtin_rvtt_ttreplay (nullptr, 4, 0, 0, 0, 0, 0);
  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
  __builtin_rvtt_ttreplay (nullptr, 4, 0, 0, 0, 0, 0);
  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
  __builtin_rvtt_ttreplay (nullptr, 4, 0, 0, 0, 0, 0);
  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
  __builtin_rvtt_ttreplay (nullptr, 4, 0, 0, 0, 0, 0);
  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
  __builtin_rvtt_ttreplay (nullptr, 4, 0, 0, 0, 0, 0);
  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
  __builtin_rvtt_ttreplay (nullptr, 4, 0, 0, 0, 0, 0);
  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
}
