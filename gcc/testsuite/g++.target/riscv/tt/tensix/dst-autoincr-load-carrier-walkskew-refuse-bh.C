// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -mtt-tensix-optimize-dst-autoincr -mtt-tensix-optimize-dst-autoincr-load-carrier -fdump-rtl-rvtt_dst_autoincr-details" }
// WALK-SKEW near-miss (lane IF replay-soundness adjudication): RWC
// effects of a replayed word are per-execution cumulative counter adds
// (WormholeB0 REPLAY.md + INCRWC.md/RWCs.md; no per-launch reset exists
// or is needed), so the ONLY skew mechanism is executions of a carried
// payload access != removed explicit increments.  Here the LAST launch
// of the shared LOAD payload has no trailing increment: rewriting the
// payload load to the carried scratch mode would advance Dst at that
// site with nothing absorbed -- the walk would skew by one stride per
// loop of the kernel.  The payload-coverage guard must refuse the whole
// payload family by name and every explicit increment must survive.
// { dg-final { scan-rtl-dump "Dst-autoincr refusal: payload execution site without matching increment .live-out RWC state." "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump-not "Dst-autoincr group:" "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-times "TTINCRWC\t0, 2, 0, 0" 9 } }
// { dg-final { scan-assembler-not "TTSETC16" } }
// { dg-final { scan-assembler-not "SFPLOAD\tL., 0, 0, 6" } }

using vec_t = __xtt_vector;

static inline void
load_row ()
{
  vec_t a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
  vec_t p0 = __builtin_rvtt_sfpmul (a, a, 0);
  vec_t p1 = __builtin_rvtt_sfpmul (p0, p0, 0);
  vec_t p2 = __builtin_rvtt_sfpmul (p1, p1, 0);
  __builtin_rvtt_sfpwritelreg (p2, 1);
}

void
load_rows_skewed ()
{
  // Covering prefix so the SETC16-to-consume distance guard is met and
  // the refusal under test (payload coverage) is the one that decides.
  vec_t acc = __builtin_rvtt_sfploadi (nullptr, 0, 0, 1, 0);
  vec_t bcc = __builtin_rvtt_sfploadi (nullptr, 0, 0, 2, 0);
  vec_t ccc = __builtin_rvtt_sfploadi (nullptr, 0, 0, 3, 0);
  acc = __builtin_rvtt_sfpmad (acc, bcc, ccc, 0);
  acc = __builtin_rvtt_sfpmad (acc, acc, acc, 0);
  __builtin_rvtt_sfpwritelreg (acc, 0);

  load_row ();
  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
  load_row ();
  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
  load_row ();
  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
  load_row ();
  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
  load_row ();
  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
  load_row ();
  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
  load_row ();
  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
  load_row ();
  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
  load_row ();
  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
  // Final row: NO trailing increment -- the uncovered execution site.
  load_row ();
}
