// { dg-do compile }
// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-dst-autoincr -fdump-rtl-rvtt_dst_autoincr-details" }
//
// The replay-expander persistence model (a BH REPLAY doc gap): the no-exec-record composition
// guard's reachability relation (block_reachable_p) is intra-function, but
// the per-thread Replay Expander buffer PERSISTS across function and
// kernel-invocation boundaries (hardware-established model:
// EXP-1 delivers a prior kernel's recorded store across a TRISC soft-reset +
// ELF reload; EXP-2 delivers across a sibling function boundary in one
// launch).  A replay-DELIVERED group in one arm of a diamond and a no-exec
// Dst-store record in the SIBLING arm are unreachable from each other by the
// forward walk, so the pre-FS guard ADMITTED (FP delta-audit probe pfj1) --
// yet a caller tile loop that alternates the arms arms the sibling record on
// one invocation and runs the group's mod-write on the next, reassembling the
// exact hardware-refuted trio (ES 2x2 / FE-F1 / FJ HANG-3).  For a replay-
// delivered group the guard now refuses ANY same-function no-exec capture,
// not only a forward-reachable one.
//
// { dg-final { scan-rtl-dump "Dst-autoincr refusal: mod-write-noexec-record-composition-unaudited .replay-delivered mod-write, no-exec replay capture in the same function .persistent replay slot, cross-invocation reassembly." "rvtt_dst_autoincr" } }

using vec_t = __xtt_vector;
volatile unsigned sink;

void
sibling_arms (unsigned mode)
{
  if (mode & 1)
    {
      // no-exec re-record with a Dst-store payload (armed, not launched here)
      __builtin_rvtt_ttreplay (nullptr, 4, 0, 0, 0, 0, 1);
      vec_t a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 3);
      vec_t p = __builtin_rvtt_sfpmul (a, a, 0);
      p = __builtin_rvtt_sfpmul (p, a, 0);
      __builtin_rvtt_sfpstore (nullptr, p, 0, 0, 0, 0, 0);
      sink = 1;
    }
  else
    {
      // replay-delivered group: launches deliver the recorded store, the
      // ttincrwc rows are the group's mod-writes.
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
}
