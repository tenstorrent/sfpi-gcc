/* Six explicit rows with a foreign effect between rows three and four.  The
   ownership proof must split the region there, leaving two groups whose
   configuration cost exceeds their removable increments, so the function is
   emitted unchanged.  DST_INTERVENE expands to the intervening statement.  */

using vec_t = __xtt_vector;

static inline void
row (unsigned addr)
{
  vec_t a = __builtin_rvtt_sfpload (nullptr, addr, 0, 0, 0, 7);
  vec_t p = __builtin_rvtt_sfpmul (a, a, 0);
  __builtin_rvtt_sfpstore (nullptr, p, addr, 0, 0, 0, 7);
  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
}

void
split_rows ()
{
  row (0);
  row (0);
  row (0);
  DST_INTERVENE;
  row (0);
  row (0);
  row (0);
}
