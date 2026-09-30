// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-dst-autoincr -fdump-rtl-rvtt_dst_autoincr-details" }
// The hardware-regressive witness class: a one-row
// rolled loop whose implicit advance would cross the backedge with zero
// covering slot words -- the mod-write's unaudited positional-state
// retirement lands on the loop-carried RWC dependence every iteration
// (measured +11.2% and +15.7% on the two whole-ELF hardware witnesses).
// One row per iteration cannot pay the audited retirement guard: the
// pass refuses by name and the function is byte-identical.
// { dg-final { scan-rtl-dump "Dst-autoincr refusal: mod-write-dominates-rolled-body .rows 1, uncovered crossing slots 2, bb \[0-9\]+." "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump-not "Dst-autoincr group:" "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-times "TTINCRWC\t0, 2, 0, 0" 1 } }
// { dg-final { scan-assembler-not "TTSETC16" } }
// { dg-final { scan-assembler-times "SFPSTORE\tL., 0, 0, 7" 1 } }

using vec_t = __xtt_vector;

void
rolled_tiny_rows ()
{
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      vec_t a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      vec_t p = __builtin_rvtt_sfpmul (a, a, 0);
      __builtin_rvtt_sfpstore (nullptr, p, 0, 0, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
