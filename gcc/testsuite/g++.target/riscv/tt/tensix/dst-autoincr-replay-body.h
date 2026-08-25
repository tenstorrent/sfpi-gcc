/* Replay-formed rows: each row is long enough for replay formation, so the
   Dst auto-increment pass sees a capture-executing first row followed by
   launch rows, each separated by a typed TTINCRWC.  The rewritten payload
   store must carry the implicit advance for every execution site.  */

using vec_t = __xtt_vector;

#ifndef DST_REPLAY_ROWS
#define DST_REPLAY_ROWS 8
#endif

static inline void
replay_row (unsigned addr)
{
  vec_t a = __builtin_rvtt_sfpload (nullptr, addr, 0, 0, 0, DST_MODE);
  vec_t p0 = __builtin_rvtt_sfpmul (a, a, 0);
  vec_t p1 = __builtin_rvtt_sfpmul (p0, p0, 0);
  vec_t p2 = __builtin_rvtt_sfpmul (p1, p1, 0);
  __builtin_rvtt_sfpstore (nullptr, p2, addr, 0, 0, 0, DST_MODE);
}

void
replayed_rows ()
{
  replay_row (0);
  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
  replay_row (0);
  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
  replay_row (0);
  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
  replay_row (0);
  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
  replay_row (0);
  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
  replay_row (0);
  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
  replay_row (0);
  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
  replay_row (0);
#if DST_REPLAY_ROWS >= 9
  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
  replay_row (0);
#endif
#if DST_REPLAY_ROWS >= 10
  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
  replay_row (0);
#endif
#ifndef DST_DROP_LAST_INCREMENT
  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
#endif
}
