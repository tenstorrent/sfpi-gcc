// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-replay-hoist -mtt-tensix-optimize-counted-row-formation -fdump-rtl-rvtt_replay" }
// Check that rows needing a register permutation are not bridged.
// { dg-final { scan-rtl-dump "counted-row-bridge-clobber: the bridges form a register cycle" "rvtt_replay" } }
// { dg-final { scan-assembler-not "SFPMOV" } }

void permuted_rows ()
{
#define ROW(RX, RY, RZ)							\
  do {									\
    __builtin_rvtt_sfpwritelreg (__builtin_rvtt_sfpload (nullptr, 64, 0, 0, 0, 7), RX); \
    __builtin_rvtt_sfpwritelreg (__builtin_rvtt_sfpload (nullptr, 80, 0, 0, 0, 7), RY); \
    __builtin_rvtt_sfpwritelreg (__builtin_rvtt_sfpload (nullptr, 96, 0, 0, 0, 7), RZ); \
    auto x = __builtin_rvtt_sfpreadlreg (RX);				\
    auto y = __builtin_rvtt_sfpreadlreg (RY);				\
    auto z = __builtin_rvtt_sfpreadlreg (RZ);				\
    __builtin_rvtt_sfpstore (nullptr, x, 0, 0, 0, 0, 7);		\
    __builtin_rvtt_sfpstore (nullptr, y, 16, 0, 0, 0, 7);		\
    __builtin_rvtt_sfpstore (nullptr, z, 32, 0, 0, 0, 7);		\
    __builtin_rvtt_sfpstore (nullptr, x, 48, 0, 0, 0, 7);		\
    __builtin_rvtt_ttincrwc (0, 4, 0, 0);				\
  } while (0)

  ROW (2, 3, 4);
  ROW (2, 4, 3);
  ROW (3, 2, 4);
  ROW (3, 4, 2);
  ROW (4, 2, 3);
  ROW (4, 3, 2);
#undef ROW
}
