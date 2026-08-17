/* The predicated-select (TTNN Where class) row in raw typed builtins.
   COND_MODE/PAYLOAD_MODE are Dst data modes; ADDR_MODE is the per-CPU
   no-increment address mode (BH 7 / WH 3); SETCC_MOD is the predicate
   sense (register-test class -- the builtin's own mask).  The
   pushc/popc pair before the loads is the typed ambient all-lanes
   enable; the pair around the merge lowers to the outermost
   SETCC/.../ENCC combine (the in-row restore).  */

#ifndef SELECT_COND_ADDR
#define SELECT_COND_ADDR 0
#define SELECT_TRUE_ADDR 32
#define SELECT_FALSE_ADDR 64
#endif
#ifndef SELECT_COND_MODE
#define SELECT_COND_MODE 2
#endif
#ifndef SELECT_PAYLOAD_MODE
#define SELECT_PAYLOAD_MODE 6
#endif
#ifndef SELECT_SETCC_MOD
#define SELECT_SETCC_MOD 2
#endif

#define SELECT_ROW()                                                          \
  do                                                                          \
    {                                                                         \
      __builtin_rvtt_sfppushc (0);                                            \
      __builtin_rvtt_sfppopc (0);                                             \
      auto condition                                                          \
	= __builtin_rvtt_sfpload (nullptr, SELECT_COND_ADDR, 0, 0,            \
				  SELECT_COND_MODE, SELECT_ADDR_MODE);        \
      auto on_true                                                            \
	= __builtin_rvtt_sfpload (nullptr, SELECT_TRUE_ADDR, 0, 0,            \
				  SELECT_PAYLOAD_MODE, SELECT_ADDR_MODE);     \
      auto on_false                                                           \
	= __builtin_rvtt_sfpload (nullptr, SELECT_FALSE_ADDR, 0, 0,           \
				  SELECT_PAYLOAD_MODE, SELECT_ADDR_MODE);     \
      __builtin_rvtt_sfppushc (0);                                            \
      __builtin_rvtt_sfpsetcc_v (condition, SELECT_SETCC_MOD);                \
      auto result = __builtin_rvtt_sfpassign_lv (on_false, on_true);          \
      __builtin_rvtt_sfppopc (0);                                             \
      __builtin_rvtt_sfpstore (nullptr, result, SELECT_COND_ADDR, 0, 0,       \
			       SELECT_PAYLOAD_MODE, SELECT_ADDR_MODE);        \
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);                                   \
    }                                                                         \
  while (0)

#define SELECT_ROWS_8()                                                       \
  do                                                                          \
    {                                                                         \
      SELECT_ROW ();                                                          \
      SELECT_ROW ();                                                          \
      SELECT_ROW ();                                                          \
      SELECT_ROW ();                                                          \
      SELECT_ROW ();                                                          \
      SELECT_ROW ();                                                          \
      SELECT_ROW ();                                                          \
      SELECT_ROW ();                                                          \
    }                                                                         \
  while (0)
