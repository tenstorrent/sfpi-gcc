// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-replay-hoist -mtt-tensix-optimize-counted-row-formation -fdump-rtl-rvtt_replay" }
// Check that rows are not bridged when an indexed SFPSWAP means index tracking may be on.
// { dg-final { scan-rtl-dump "shadow-state-unproved: a bridge writes the value bank" "rvtt_replay" } }
// { dg-final { scan-assembler-not "SFPMOV" } }

void shifted_rows_indexed ()
{
  {
    auto v0 = __builtin_rvtt_sfpreadlreg (0);
    auto v2 = __builtin_rvtt_sfpreadlreg (2);
    auto i0 = __builtin_rvtt_sfpreadlreg (4);
    auto i2 = __builtin_rvtt_sfpreadlreg (6);
    auto r = __builtin_rvtt_sfpswap_indexed (v0, v2, i0, i2, 8);
    __builtin_rvtt_sfpwritelreg (__builtin_rvtt_sfpselect4 (r, 0), 0);
    __builtin_rvtt_sfpwritelreg (__builtin_rvtt_sfpselect4 (r, 1), 2);
    __builtin_rvtt_sfpwritelreg (__builtin_rvtt_sfpselect4 (r, 2), 4);
    __builtin_rvtt_sfpwritelreg (__builtin_rvtt_sfpselect4 (r, 3), 6);
  }

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

  ROW (0, 1);
  ROW (1, 2);
  ROW (0, 2);
  ROW (1, 3);
  ROW (0, 3);
  ROW (2, 3);
#undef ROW
}
