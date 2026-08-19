// The WP15 upward-carrier fire row (macro-planner-ims-carrier-fire-bh.C),
// shared by its renamed and varied twins.  SHIFT and ADDR parameterize
// the constant-variation twin (value-independence proof).
#define ROW(SHIFT, ADDR)                                                      \
  do                                                                          \
    {                                                                          \
      __builtin_rvtt_sfppushc (0);                                            \
      __builtin_rvtt_sfppopc (0);                                             \
      auto a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 4, 7);               \
      a = __builtin_rvtt_sfpcast (a, 3);                                      \
      auto b = __builtin_rvtt_sfpload (nullptr, ADDR, 0, 0, 4, 7);            \
      auto sb = __builtin_rvtt_sfpshft_i (nullptr, b, SHIFT, 0, 0, 0);        \
      auto c0 = __builtin_rvtt_sfpmul24 (a, sb, 0);                           \
      b = __builtin_rvtt_sfpcast (b, 3);                                      \
      auto lo = __builtin_rvtt_sfpmul24 (a, b, 0);                            \
      auto hi = __builtin_rvtt_sfpmul24 (a, b, 1);                            \
      lo = __builtin_rvtt_sfpshft_i (nullptr, lo, 1, 0, 0, 0);                \
      b = __builtin_rvtt_sfpshft_i (nullptr, b, SHIFT, 0, 0, 0);              \
      hi = __builtin_rvtt_sfpiadd_v (hi, c0, 4);                              \
      b = __builtin_rvtt_sfpmul24 (a, b, 0);                                  \
      hi = __builtin_rvtt_sfpiadd_v (hi, b, 4);                               \
      hi = __builtin_rvtt_sfpshft_i (nullptr, hi, 23, 0, 0, 0);               \
      lo = __builtin_rvtt_sfpiadd_v (lo, hi, 4);                              \
      lo = __builtin_rvtt_sfpcast (lo, 3);                                    \
      __builtin_rvtt_sfpstore (nullptr, lo, 0, 0, 0, 4, 7);                   \
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);                                   \
    }                                                                          \
  while (0)
