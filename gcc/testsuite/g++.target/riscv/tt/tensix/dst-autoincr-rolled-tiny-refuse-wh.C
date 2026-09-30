// { dg-do compile }
// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-dst-autoincr -fdump-rtl-rvtt_dst_autoincr-details" }
// Wormhole twin of the rolled-tiny refusal: the crossing charge is
// capability-table generic, not a Blackhole special case.  (The default
// Wormhole replay formation reshapes the skinny body's delivery, so the
// covering count differs from Blackhole's, but the iteration still stops
// short of the drained-frontend window and the refusal holds.)
// { dg-final { scan-rtl-dump "Dst-autoincr refusal: mod-write-dominates-rolled-body .rows 1, uncovered crossing slots \[0-9\]+, bb \[0-9\]+." "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump-not "Dst-autoincr group:" "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-times "TTINCRWC\t0, 2, 0, 0" 1 } }
// { dg-final { scan-assembler-not "TTSETC16" } }
// { dg-final { scan-assembler-times "SFPSTORE\tL., 0, 0, 3" 1 } }

using vec_t = __xtt_vector;

void
rolled_tiny_rows ()
{
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      vec_t a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 3);
      vec_t p = __builtin_rvtt_sfpmul (a, a, 0);
      __builtin_rvtt_sfpstore (nullptr, p, 0, 0, 0, 0, 3);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
