// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-dst-autoincr -fdump-rtl-rvtt_dst_autoincr-details" }
// Check the sibling-arm no-exec capture refusal with renamed identifiers and varied constants.
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
