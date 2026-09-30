// { dg-do compile }
// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-dst-autoincr -fdump-rtl-rvtt_dst_autoincr-details" }
// Silicon-refuted delivery boundary of the composition guard's distance
// proxy (rvtt-cost.md "no-exec record composition"):
// a REPLAY-DELIVERED mod-write (the group's rows are launches whose
// payload carries the terminator store) composed with a no-exec capture
// reachable from the group refuses at ANY frontend-word distance.  The
// covering words here (the unconditional scalar loop between the
// launches and the backedge) satisfy the audited W_drain window, which
// is exactly how the sparse_k_filter shape was admitted at pin 19 --
// and that byte-identical binary wedges Tensix at runtime trip 32 while
// passing at trip 8: launch expansion breaks the issue-parity premise
// of the frontend-word count, so no static distance is audited.
// Wormhole adopts the same-frontend-class conservative verdict
// (dump-only scans: default Wormhole replay formation reshapes
// delivery).
// { dg-final { scan-rtl-dump "Dst-autoincr refusal: mod-write-noexec-record-composition-unaudited .replay-delivered mod-write, no-exec replay capture reachable from the group, bb \[0-9\]+, capture bb \[0-9\]+." "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump-not "Dst-autoincr group:" "rvtt_dst_autoincr" } }

using vec_t = __xtt_vector;

volatile unsigned sink;

void
launch_rows_reachable_noexec_record_wh (unsigned faces)
{
  for (unsigned face = 0; face != faces; ++face)
    {
      // Re-recorded per face (no-exec): the payload ends in the no-inc
      // store the launches deliver -- the group's own mod-write is
      // replay-delivered.
      __builtin_rvtt_ttreplay (nullptr, 4, 0, 0, 0, 0, 1);
      vec_t a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 3);
      vec_t p = __builtin_rvtt_sfpmul (a, a, 0);
      p = __builtin_rvtt_sfpmul (p, a, 0);
      __builtin_rvtt_sfpstore (nullptr, p, 0, 0, 0, 0, 3);

      // Control flow between the record and the launches keeps the
      // capture in its own block (the sparse_k_filter layout).
      if (face & 1)
	sink = face;

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

      // Unconditional covering block on every backedge path: more
      // frontend issue-slot words than the audited W_drain window.
      for (unsigned k = 0; k != 4; ++k)
	sink = sink + k * face;
    }
}
