/* Two row groups in one block, separated by a typed TTINCRWC that advances
   a non-Dst counter: the separator splits the ownership groups (it is not
   gap legal) but cannot write address-modifier configuration, so a single
   dominating slot program placed before the first group serves both.
   Profitability is evaluated for the shared program: each group alone would
   not pay for the configuration.  */

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
}
