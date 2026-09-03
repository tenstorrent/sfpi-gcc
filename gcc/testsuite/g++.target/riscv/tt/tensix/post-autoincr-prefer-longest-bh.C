// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -mtt-tensix-optimize-dst-autoincr -mtt-tensix-optimize-post-autoincr-window -mtt-tensix-post-autoincr-window-prefer-longest -fdump-rtl-rvtt_replay_reform-details" }
// Measurement knob -mtt-tensix-post-autoincr-window-prefer-longest
// switches the re-formation candidate selection to a length-major key
// (word saving as tie-break).  On the established single-candidate
// re-formation fire shape the selected capture is the same run, so the
// knob must not disturb the fold: the carried payload still forms and
// its launch arithmetic is still proven.  (A selection difference needs
// competing candidates of different lengths; the knob exists for
// hardware A/B measurement, not codegen improvement.)
// { dg-final { scan-rtl-dump "post-autoincr-window: carried payload launch arithmetic proven" "rvtt_replay_reform" } }
// { dg-final { scan-rtl-dump "Capturing and executing sequence" "rvtt_replay_reform" } }
// { dg-final { scan-assembler "TTREPLAY" } }
// { dg-final { scan-assembler-not "TTINCRWC" } }

using vec_t = __xtt_vector;

void
carried_rows_reform (void)
{
  // Covering prefix for the SETC16-to-consume distance guard.
  vec_t acc = __builtin_rvtt_sfploadi (nullptr, 0, 0, 1, 0);
  vec_t bcc = __builtin_rvtt_sfploadi (nullptr, 0, 0, 2, 0);
  vec_t ccc = __builtin_rvtt_sfploadi (nullptr, 0, 0, 3, 0);
  acc = __builtin_rvtt_sfpmad (acc, bcc, ccc, 0);
  acc = __builtin_rvtt_sfpmad (acc, acc, acc, 0);

  // LOAD-terminated rows (reduction walk): fold -> carried mode-6 loads.
#define ROW \
  { vec_t a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7); \
    acc = __builtin_rvtt_sfpmad (acc, a, acc, 0); \
    __builtin_rvtt_ttincrwc (0, 2, 0, 0); }
  ROW ROW ROW ROW ROW ROW ROW ROW
  ROW ROW ROW ROW ROW ROW ROW ROW
  __builtin_rvtt_sfpstore (nullptr, acc, 0, 0, 0, 0, 7);
}
