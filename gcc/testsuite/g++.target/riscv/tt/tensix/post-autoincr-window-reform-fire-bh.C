// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -mtt-tensix-optimize-dst-autoincr -mtt-tensix-optimize-post-autoincr-window -fdump-rtl-rvtt_dst_autoincr -fdump-rtl-rvtt_replay_reform-details" }
// Post-autoincr window RE-FORMATION (lane IH): before the fold each row
// ends in an explicit TTINCRWC -- a window-excluded barrier word -- so
// the FIRST replay formation cannot capture the rows (2-word runs
// between barriers are under MIN_SEQUENCE).  The Dst auto-increment
// fold absorbs the separators (rows retarget to the compiler-owned
// scratch modifier 6) and the body becomes one word-uniform run; the
// re-formation pass then captures it, delivering the repeated rows
// through replay launches.  The carried payload's launch arithmetic
// (delivered executions == replaced row sites) is proven and dumped.
// { dg-final { scan-rtl-dump "Dst-autoincr group: bb \[0-9\]+ rows 16 stride 2 config 3 words" "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump "post-autoincr-window: carried payload launch arithmetic proven" "rvtt_replay_reform" } }
// { dg-final { scan-rtl-dump "Capturing and executing sequence" "rvtt_replay_reform" } }
// { dg-final { scan-assembler-not "TTINCRWC" } }
// { dg-final { scan-assembler "TTREPLAY" } }
// { dg-final { scan-assembler-times "SFPLOAD\tL., 0, 0, 6" 4 } }

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
