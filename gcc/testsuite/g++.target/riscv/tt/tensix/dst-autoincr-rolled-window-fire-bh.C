// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-dst-autoincr -fdump-rtl-rvtt_dst_autoincr-details" }
// The measured-WIN twin of the rolled-tiny refusal (the unaryshift hand
// kernel's -23.6% shape): the SAME live backedge crossing, but eight rows
// per iteration pay the crossing charge with slots to spare -- the
// transform keeps firing and the emission is unchanged by the pricing
// term.
// { dg-final { scan-rtl-dump-times "Dst-autoincr group: bb \[0-9\]+ rows 8 stride 2 config 3 words .preheader." 1 "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump "Dst-autoincr: mod-write backedge crossing priced .rows 8, uncovered crossing slots 2, bb \[0-9\]+." "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump-not "mod-write-dominates-rolled-body" "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-not "TTINCRWC" } }
// { dg-final { scan-assembler-times "TTSETC16\t18, 0" 1 } }
// { dg-final { scan-assembler-times "TTSETC16\t34, 2" 1 } }
// { dg-final { scan-assembler-times "TTSETC16\t53, 0" 1 } }
// { dg-final { scan-assembler-times "SFPSTORE\tL., 0, 0, 6" 8 } }

using vec_t = __xtt_vector;

static inline void
row ()
{
  vec_t a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
  vec_t p = __builtin_rvtt_sfpmul (a, a, 0);
  __builtin_rvtt_sfpstore (nullptr, p, 0, 0, 0, 0, 7);
  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
}

void
rolled_window_rows ()
{
  for (unsigned ix = 0; ix != 4; ++ix)
    {
      row ();
      row ();
      row ();
      row ();
      row ();
      row ();
      row ();
      row ();
    }
}
