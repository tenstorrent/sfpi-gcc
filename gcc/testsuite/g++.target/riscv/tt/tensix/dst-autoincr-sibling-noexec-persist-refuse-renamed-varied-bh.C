// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-dst-autoincr -fdump-rtl-rvtt_dst_autoincr-details" }
//
// lane FS (FP-3) renamed/varied twin of the sibling-arm persistence refusal
// (over-fit guard: different function/variable names, six launches instead of
// eight, a longer payload mul chain, extra scalar drain).  The mechanism must
// still refuse: a replay-delivered group in one arm and a no-exec Dst-store
// record in the sibling arm reassemble across caller-loop invocations because
// the Replay Expander buffer persists (laneFS-evidence-20260822).
//
// { dg-final { scan-rtl-dump "Dst-autoincr refusal: mod-write-noexec-record-composition-unaudited .replay-delivered mod-write, no-exec replay capture in the same function .persistent replay slot, cross-invocation reassembly." "rvtt_dst_autoincr" } }

using vec_t = __xtt_vector;
volatile unsigned drain;

void
alt_faces (unsigned sel)
{
  if (sel & 1)
    {
      // no-exec re-record with a Dst-store payload (longer mul chain).
      __builtin_rvtt_ttreplay (nullptr, 5, 0, 0, 0, 0, 1);
      vec_t u = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      vec_t w = __builtin_rvtt_sfpmul (u, u, 0);
      w = __builtin_rvtt_sfpmul (w, u, 0);
      w = __builtin_rvtt_sfpmul (w, u, 0);
      w = __builtin_rvtt_sfpmul (w, u, 0);
      __builtin_rvtt_sfpstore (nullptr, w, 0, 0, 0, 0, 7);
      drain = sel + 3u;
    }
  else
    {
      // replay-delivered group: six launches deliver the recorded store.
      __builtin_rvtt_ttreplay (nullptr, 5, 0, 0, 0, 0, 0);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
      __builtin_rvtt_ttreplay (nullptr, 5, 0, 0, 0, 0, 0);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
      __builtin_rvtt_ttreplay (nullptr, 5, 0, 0, 0, 0, 0);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
      __builtin_rvtt_ttreplay (nullptr, 5, 0, 0, 0, 0, 0);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
      __builtin_rvtt_ttreplay (nullptr, 5, 0, 0, 0, 0, 0);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
      __builtin_rvtt_ttreplay (nullptr, 5, 0, 0, 0, 0, 0);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
