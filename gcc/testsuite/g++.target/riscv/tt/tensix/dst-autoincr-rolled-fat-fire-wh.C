// { dg-do compile }
// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-dst-autoincr -fdump-rtl-rvtt_dst_autoincr-details" }
// Wormhole twin of the fat-body covered fire: the drained-frontend window
// is capability-table generic (same-frontend-class conservative adoption,
// rvtt-cost.md), not a Blackhole special case.  (The default Wormhole
// replay formation folds the body into a capture window plus a launch;
// the members, the launch word floor, and the scalar loop control still
// cover the window.)
// { dg-final { scan-rtl-dump-times "Dst-autoincr group: bb \[0-9\]+ rows 1 stride 2 config 3 words .preheader." 1 "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump "Dst-autoincr: mod-write backedge crossing covered .rows 1, iteration slot words \[0-9\]+ >= drain window 7, bb \[0-9\]+." "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump-not "mod-write-dominates-rolled-body" "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-not "TTINCRWC" } }
// { dg-final { scan-assembler-times "TTSETC16\t29, 2" 1 } }
// { dg-final { scan-assembler-times "SFPSTORE\tL., 0, 0, 2" 1 } }

using vec_t = __xtt_vector;

void
rolled_fat_rows ()
{
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      vec_t a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 3);
      vec_t p = __builtin_rvtt_sfpmul (a, a, 0);
      p = __builtin_rvtt_sfpmul (p, a, 0);
      p = __builtin_rvtt_sfpmul (p, a, 0);
      p = __builtin_rvtt_sfpmul (p, a, 0);
      p = __builtin_rvtt_sfpmul (p, a, 0);
      p = __builtin_rvtt_sfpmul (p, a, 0);
      __builtin_rvtt_sfpstore (nullptr, p, 0, 0, 0, 0, 3);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
