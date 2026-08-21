// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-dst-autoincr -fdump-rtl-rvtt_dst_autoincr-details" }
// The break-even of the mod-write crossing charge falls out of the audited
// positional-state retirement guard (two slots), not a tuned trip count:
// two rows per iteration cannot pay it (2 <= 2, refuse by name), three
// rows can (net one slot per iteration, fire).
// { dg-final { scan-rtl-dump "Dst-autoincr refusal: mod-write-dominates-rolled-body .rows 2, uncovered crossing slots 2, bb \[0-9\]+." "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump-times "Dst-autoincr group: bb \[0-9\]+ rows 3 stride 2 config 3 words .preheader." 1 "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump "Dst-autoincr: mod-write backedge crossing priced .rows 3, uncovered crossing slots 2, bb \[0-9\]+." "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-times "TTINCRWC\t0, 2, 0, 0" 2 } }
// { dg-final { scan-assembler-times "SFPSTORE\tL., 0, 0, 6" 3 } }
// { dg-final { scan-assembler-times "SFPSTORE\tL., 0, 0, 7" 2 } }

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
two_rows_refuse ()
{
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      row ();
      row ();
    }
}

void
three_rows_fire ()
{
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      row ();
      row ();
      row ();
    }
}
