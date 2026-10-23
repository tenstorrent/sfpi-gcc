/* Test fixture prologue: establish physical flags and LaneConfig, rather
   than relying on an undocumented function-entry all-lanes convention.  */
#ifndef RVTT_TEST_LANE_RESET
#define RVTT_TEST_LANE_RESET() do { \
  __builtin_rvtt_sfpencc (10, 3); \
  __builtin_rvtt_sfpconfig_i (0, 15, 1); \
} while (0)
#endif
