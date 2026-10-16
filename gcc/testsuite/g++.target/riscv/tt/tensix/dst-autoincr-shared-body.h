/* Two row groups in one block separated by a non-Dst TTINCRWC.  */

using vec_t = __xtt_vector;

static inline void
SHARED_ROW (unsigned addr)
{
  vec_t a = __builtin_rvtt_sfpload (nullptr, addr, 0, 0, 0, DST_MODE);
  vec_t p = __builtin_rvtt_sfpmul (a, a, 0);
  __builtin_rvtt_sfpstore (nullptr, p, addr, 0, 0, 0, DST_MODE);
  __builtin_rvtt_ttincrwc (0, DST_STRIDE, 0, 0);
}

void
SHARED_FN ()
{
#if DST_GROUP1 >= 1
  SHARED_ROW (DST_ADDR);
#endif
#if DST_GROUP1 >= 2
  SHARED_ROW (DST_ADDR);
#endif
#if DST_GROUP1 >= 3
  SHARED_ROW (DST_ADDR);
#endif
#if DST_GROUP1 >= 4
  SHARED_ROW (DST_ADDR);
#endif
#if DST_GROUP1 >= 5
  SHARED_ROW (DST_ADDR);
#endif
#if DST_GROUP1 >= 6
  SHARED_ROW (DST_ADDR);
#endif
  __builtin_rvtt_ttincrwc (0, 0, 1, 0);
#if DST_GROUP2 >= 1
  SHARED_ROW (DST_ADDR);
#endif
#if DST_GROUP2 >= 2
  SHARED_ROW (DST_ADDR);
#endif
#if DST_GROUP2 >= 3
  SHARED_ROW (DST_ADDR);
#endif
#if DST_GROUP2 >= 4
  SHARED_ROW (DST_ADDR);
#endif
#if DST_GROUP2 >= 5
  SHARED_ROW (DST_ADDR);
#endif
#if DST_GROUP2 >= 6
  SHARED_ROW (DST_ADDR);
#endif
}
