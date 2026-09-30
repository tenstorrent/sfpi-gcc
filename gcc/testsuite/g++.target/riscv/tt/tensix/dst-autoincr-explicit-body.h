/* Explicit unrolled rows: each row performs a no-increment load/compute/store
   and advances Dst with a typed TTINCRWC of constant stride.  Rows are short
   enough that replay formation never triggers, so the Dst auto-increment
   pass sees the explicit shape.  */

using vec_t = __xtt_vector;

static inline void
row (unsigned addr)
{
  vec_t a = __builtin_rvtt_sfpload (nullptr, addr, 0, 0, 0, DST_MODE);
  vec_t p = __builtin_rvtt_sfpmul (a, a, 0);
  __builtin_rvtt_sfpstore (nullptr, p, addr, 0, 0, 0, DST_MODE);
  __builtin_rvtt_ttincrwc (0, DST_STRIDE, 0, 0);
}

void
unrolled_rows ()
{
#if DST_ROWS >= 1
  row (DST_ADDR);
#endif
#if DST_ROWS >= 2
  row (DST_ADDR);
#endif
#if DST_ROWS >= 3
  row (DST_ADDR);
#endif
#if DST_ROWS >= 4
  row (DST_ADDR);
#endif
#if DST_ROWS >= 5
  row (DST_ADDR);
#endif
#if DST_ROWS >= 6
  row (DST_ADDR);
#endif
#if DST_ROWS >= 7
  row (DST_ADDR);
#endif
#if DST_ROWS >= 8
  row (DST_ADDR);
#endif
#if DST_ROWS >= 9
  row (DST_ADDR);
#endif
#if DST_ROWS >= 10
  row (DST_ADDR);
#endif
#if DST_ROWS >= 11
  row (DST_ADDR);
#endif
#if DST_ROWS >= 12
  row (DST_ADDR);
#endif
#if DST_ROWS >= 13
  row (DST_ADDR);
#endif
#if DST_ROWS >= 14
  row (DST_ADDR);
#endif
#if DST_ROWS >= 15
  row (DST_ADDR);
#endif
#if DST_ROWS >= 16
  row (DST_ADDR);
#endif
}
