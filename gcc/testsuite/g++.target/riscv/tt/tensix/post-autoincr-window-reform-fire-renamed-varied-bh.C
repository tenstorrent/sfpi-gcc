// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -mtt-tensix-optimize-dst-autoincr -mtt-tensix-optimize-post-autoincr-window -fdump-rtl-rvtt_dst_autoincr -fdump-rtl-rvtt_replay_reform-details" }
// Renamed/varied twin of post-autoincr-window-reform-fire-bh.C: twelve
// three-word rows (load + two mads) at stride 4.  The fold absorbs the
// separators; the re-formation captures the word-uniform body and
// proves the carried launch arithmetic.  Nothing about the mechanism
// keys on names, row counts, word counts, or strides.
// { dg-final { scan-rtl-dump "Dst-autoincr group: bb \[0-9\]+ rows 12 stride 4 config 3 words" "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump "post-autoincr-window: carried payload launch arithmetic proven" "rvtt_replay_reform" } }
// { dg-final { scan-rtl-dump "Capturing and executing sequence" "rvtt_replay_reform" } }
// { dg-final { scan-assembler-not "TTINCRWC" } }
// { dg-final { scan-assembler "TTREPLAY" } }

using vec_t = __xtt_vector;

void
walker_dozen (void)
{
  vec_t sum = __builtin_rvtt_sfploadi (nullptr, 0, 0, 4, 0);
  vec_t alt = __builtin_rvtt_sfploadi (nullptr, 0, 0, 5, 0);
  vec_t mix = __builtin_rvtt_sfploadi (nullptr, 0, 0, 6, 0);
  sum = __builtin_rvtt_sfpmad (sum, alt, mix, 0);
  sum = __builtin_rvtt_sfpmad (sum, sum, sum, 0);

#define WROW \
  { vec_t v = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7); \
    sum = __builtin_rvtt_sfpmad (sum, v, sum, 0); \
    alt = __builtin_rvtt_sfpmad (alt, v, alt, 0); \
    __builtin_rvtt_ttincrwc (0, 4, 0, 0); }
  WROW WROW WROW WROW WROW WROW
  WROW WROW WROW WROW WROW WROW
  sum = __builtin_rvtt_sfpmad (sum, alt, sum, 0);
  __builtin_rvtt_sfpstore (nullptr, sum, 0, 0, 0, 0, 7);
}
