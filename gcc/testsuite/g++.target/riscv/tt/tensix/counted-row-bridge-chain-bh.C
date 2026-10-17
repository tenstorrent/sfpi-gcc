// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-replay-hoist -mtt-tensix-optimize-counted-row-formation -fdump-rtl-rvtt_replay" }
// Check that bridging register moves L7 <- L6 and L6 <- L5 are issued in dependency order.
// { dg-final { scan-rtl-dump "Canonicalized counted-row family" "rvtt_replay" } }
// { dg-final { scan-assembler {SFPMOV\tL7, L6, 2\n\tSFPMOV\tL6, L5, 2} } }
// { dg-final { scan-assembler-not {SFPMOV\tL6, L5, 2\n\tSFPMOV\tL7, L6, 2} } }

void shifted_rows ()
{
#define ROW(RX, RY)							\
  do {									\
    __builtin_rvtt_sfpwritelreg (__builtin_rvtt_sfpload (nullptr, 64, 0, 0, 0, 7), RX); \
    __builtin_rvtt_sfpwritelreg (__builtin_rvtt_sfpload (nullptr, 80, 0, 0, 0, 7), RY); \
    auto x = __builtin_rvtt_sfpreadlreg (RX);				\
    auto y = __builtin_rvtt_sfpreadlreg (RY);				\
    __builtin_rvtt_sfpstore (nullptr, x, 0, 0, 0, 0, 7);		\
    __builtin_rvtt_sfpstore (nullptr, y, 16, 0, 0, 0, 7);		\
    __builtin_rvtt_sfpstore (nullptr, y, 48, 0, 0, 0, 7);		\
    __builtin_rvtt_sfpstore (nullptr, x, 32, 0, 0, 0, 7);		\
    __builtin_rvtt_ttincrwc (0, 4, 0, 0);				\
  } while (0)

  ROW (2, 3);
  ROW (3, 4);
  ROW (4, 5);
  ROW (5, 6);
  ROW (6, 7);
  ROW (2, 3);
#undef ROW
}
