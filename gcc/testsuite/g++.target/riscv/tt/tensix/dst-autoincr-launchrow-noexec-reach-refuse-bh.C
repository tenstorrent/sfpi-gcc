// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-dst-autoincr -fdump-rtl-rvtt_dst_autoincr-details" }
// Check that a replay-delivered mod-write reachable from a no-exec capture refuses at any distance.
// { dg-final { scan-rtl-dump "Dst-autoincr refusal: mod-write-noexec-record-composition-unaudited .replay-delivered mod-write, no-exec replay capture reachable from the group, bb \[0-9\]+, capture bb \[0-9\]+." "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump-not "Dst-autoincr group:" "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler "TTINCRWC\t0, 2, 0, 0" } }
// { dg-final { scan-assembler-not "TTSETC16\t34" } }

using vec_t = __xtt_vector;

volatile unsigned sink;

void
launch_rows_reachable_noexec_record (unsigned faces)
{
  for (unsigned face = 0; face != faces; ++face)
    {
      // Re-recorded per face (no-exec): the payload ends in the no-inc
      // store the launches deliver -- the group's own mod-write is
      // replay-delivered.
      __builtin_rvtt_ttreplay (nullptr, 4, 0, 0, 0, 0, 1);
      vec_t a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      vec_t p = __builtin_rvtt_sfpmul (a, a, 0);
      p = __builtin_rvtt_sfpmul (p, a, 0);
      __builtin_rvtt_sfpstore (nullptr, p, 0, 0, 0, 0, 7);

      // Control flow between the record and the launches keeps the
      // capture in its own block.
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
      // frontend words than the W_drain window.
      for (unsigned k = 0; k != 4; ++k)
	sink = sink + k * face;
    }
}
