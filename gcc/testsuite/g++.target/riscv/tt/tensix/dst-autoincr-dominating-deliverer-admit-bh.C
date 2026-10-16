// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-dst-autoincr -mtt-tensix-optimize-replay-hoist -mtt-tensix-optimize-replay-record-hoist -fdump-rtl-rvtt_dst_autoincr-details" }
// Check that a dominating no-exec record in the entry block admits the replay-delivered group.
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
  __builtin_rvtt_ttreplay (nullptr, 4, 0, 0, 0, 0, 0);
  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
  __builtin_rvtt_ttreplay (nullptr, 4, 0, 0, 0, 0, 0);
  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
}
