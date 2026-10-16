// Check the replay-delivered ENABLE_DEST_INDEX window violation on Wormhole.
// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-crosslane" }

using vec_t = __xtt_vector;

#define ROW do {                                                 \
    vec_t xa = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 0);  \
    __builtin_rvtt_sfpwritelreg (xa, 6);                         \
    vec_t xb = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 0);  \
    __builtin_rvtt_sfpwritelreg (xb, 7);                         \
    vec_t y = __builtin_rvtt_sfpmul (xa, xb, 0);                 \
    __builtin_rvtt_sfpwritelreg (y, 5);                          \
    __builtin_rvtt_sfpstore (nullptr, y, 0, 0, 0, 0, 0);         \
  } while (0)

void delivered_violation_wh ()
{
  ROW;
  __builtin_rvtt_sfpconfig_i (0x4, 15, 1);
  ROW; ROW; ROW;
  __builtin_rvtt_sfpconfig_i (0x0, 15, 1);
}

// { dg-error "dest-index-window-violation: replay launch delivers" "" { target *-*-* } 0 }
