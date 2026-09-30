// { dg-do compile }
// { dg-options "-mcpu=tt-wh-tensix -O2 -mtt-tensix-optimize-dst-autoincr -mtt-tensix-optimize-post-autoincr-window -fdump-rtl-rvtt_dst_autoincr -fdump-rtl-rvtt_replay_reform-details" }
// Wormhole twin of post-autoincr-window-reform-fire-bh.C.  Wormhole
// no-increment modifier 3, compiler-owned scratch modifier 2 (physical
// slot 6): the fold retargets the rows to mode 2 and absorbs the
// separators; the re-formation captures the now word-uniform body and
// proves the carried launch arithmetic.
// { dg-final { scan-rtl-dump "Dst-autoincr group: bb \[0-9\]+ rows 16 stride 2 config 3 words" "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump "post-autoincr-window: carried payload launch arithmetic proven" "rvtt_replay_reform" } }
// { dg-final { scan-rtl-dump "Capturing and executing sequence" "rvtt_replay_reform" } }
// { dg-final { scan-assembler-not "TTINCRWC" } }
// { dg-final { scan-assembler "TTREPLAY" } }
// { dg-final { scan-assembler-times "SFPLOAD\tL., 0, 0, 2" 4 } }

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
  { vec_t a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 3); \
    acc = __builtin_rvtt_sfpmad (acc, a, acc, 0); \
    __builtin_rvtt_ttincrwc (0, 2, 0, 0); }
  ROW ROW ROW ROW ROW ROW ROW ROW
  ROW ROW ROW ROW ROW ROW ROW ROW
  __builtin_rvtt_sfpstore (nullptr, acc, 0, 0, 0, 0, 3);
}
