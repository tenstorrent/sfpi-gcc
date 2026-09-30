// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -mtt-tensix-optimize-dst-autoincr -fdump-rtl-rvtt_dst_autoincr" }
// Knob-off half of post-autoincr-window-reform-fire-bh.C: the fold
// still fires (rows carried, separators absorbed) but with
// -mtt-tensix-optimize-post-autoincr-window off the re-formation pass
// does not run: no replay window forms over the folded rows and all 16
// carried loads stay inline (the load-carrier delivery shape,
// byte-identical to the pre-lane-IH pipeline).
// { dg-final { scan-rtl-dump "Dst-autoincr group: bb \[0-9\]+ rows 16 stride 2 config 3 words" "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-not "TTINCRWC" } }
// { dg-final { scan-assembler-not "TTREPLAY" } }
// { dg-final { scan-assembler-times "SFPLOAD\tL., 0, 0, 6" 16 } }

using vec_t = __xtt_vector;

void
carried_rows_reform (void)
{
  vec_t acc = __builtin_rvtt_sfploadi (nullptr, 0, 0, 1, 0);
  vec_t bcc = __builtin_rvtt_sfploadi (nullptr, 0, 0, 2, 0);
  vec_t ccc = __builtin_rvtt_sfploadi (nullptr, 0, 0, 3, 0);
  acc = __builtin_rvtt_sfpmad (acc, bcc, ccc, 0);
  acc = __builtin_rvtt_sfpmad (acc, acc, acc, 0);

#define ROW \
  { vec_t a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7); \
    acc = __builtin_rvtt_sfpmad (acc, a, acc, 0); \
    __builtin_rvtt_ttincrwc (0, 2, 0, 0); }
  ROW ROW ROW ROW ROW ROW ROW ROW
  ROW ROW ROW ROW ROW ROW ROW ROW
  __builtin_rvtt_sfpstore (nullptr, acc, 0, 0, 0, 0, 7);
}
