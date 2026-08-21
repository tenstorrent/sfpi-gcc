// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-dst-autoincr -fdump-rtl-rvtt_dst_autoincr-details" }
// Renamed-and-varied twin of the rolled-window fire: different function
// names, compute chain, Dst address, stride and trip count keep firing --
// the pricing keys on rows-per-crossing, never on kernel identity or
// constants.
// { dg-final { scan-rtl-dump-times "Dst-autoincr group: bb \[0-9\]+ rows 6 stride 4 config 3 words .preheader." 1 "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump "Dst-autoincr: mod-write backedge crossing priced .rows 6, uncovered crossing slots 2, bb \[0-9\]+." "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump-not "mod-write-dominates-rolled-body" "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-not "TTINCRWC" } }
// { dg-final { scan-assembler-times "TTSETC16\t34, 4" 1 } }
// { dg-final { scan-assembler-times "SFPSTORE\tL., 3, 0, 6" 6 } }

using vec_t = __xtt_vector;

static inline void
some_other_row_helper ()
{
  vec_t v = __builtin_rvtt_sfpload (nullptr, 3, 0, 0, 0, 7);
  vec_t w = __builtin_rvtt_sfpadd (v, v, 0);
  __builtin_rvtt_sfpstore (nullptr, w, 3, 0, 0, 0, 7);
  __builtin_rvtt_ttincrwc (0, 4, 0, 0);
}

void
another_kernel_entirely (unsigned n)
{
  for (unsigned qq = 0; qq != n; ++qq)
    {
      some_other_row_helper ();
      some_other_row_helper ();
      some_other_row_helper ();
      some_other_row_helper ();
      some_other_row_helper ();
      some_other_row_helper ();
    }
}
