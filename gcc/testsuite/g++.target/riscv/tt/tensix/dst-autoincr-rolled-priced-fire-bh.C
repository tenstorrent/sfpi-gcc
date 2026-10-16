// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-dst-autoincr -fdump-rtl-rvtt_dst_autoincr-details" }
// Check that a two-row loop one slot short of the drained-frontend window pays the charge and fires.
// { dg-final { scan-rtl-dump-times "Dst-autoincr group: bb \[0-9\]+ rows 2 stride 2 config 3 words .preheader." 1 "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump "Dst-autoincr: mod-write backedge crossing priced .rows 2, uncovered crossing slots 1, bb \[0-9\]+." "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump-not "mod-write-dominates-rolled-body" "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-not "TTINCRWC" } }
// { dg-final { scan-assembler-times "TTSETC16\t34, 2" 1 } }
// { dg-final { scan-assembler-times "SFPSTORE\tL., 0, 0, 6" 2 } }

using vec_t = __xtt_vector;

void
rolled_priced_rows ()
{
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      vec_t a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      vec_t p = __builtin_rvtt_sfpmul (a, a, 0);
      __builtin_rvtt_sfpstore (nullptr, p, 0, 0, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
      __builtin_rvtt_sfpstore (nullptr, p, 0, 0, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
