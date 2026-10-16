// Check that a non-exempt LReg5 write replay-delivered inside an ENABLE_DEST_INDEX
// window is an error, reported once per recorded instruction.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-crosslane" }

using vec_t = __xtt_vector;

#define ROW do {                                                 \
    vec_t xa = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);  \
    __builtin_rvtt_sfpwritelreg (xa, 6);                         \
    vec_t xb = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);  \
    __builtin_rvtt_sfpwritelreg (xb, 7);                         \
    vec_t y = __builtin_rvtt_sfpand (xa, xb);                    \
    __builtin_rvtt_sfpwritelreg (y, 5);                          \
    __builtin_rvtt_sfpstore (nullptr, y, 0, 0, 0, 0, 7);         \
  } while (0)

void delivered_violation ()
{
  ROW;                                     // record site (window closed)
  __builtin_rvtt_sfpconfig_i (0x4, 15, 1); // OPEN
  ROW; ROW; ROW;                           // launches in-window
  __builtin_rvtt_sfpconfig_i (0x0, 15, 1); // CLOSE
}

// { dg-error "dest-index-window-violation: replay launch delivers" "" { target *-*-* } 0 }
