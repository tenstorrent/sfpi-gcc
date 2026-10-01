// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-replay-hoist -mtt-tensix-optimize-counted-row-formation -fdump-rtl-rvtt_replay -DROW_IMM_BASE=0x11220000" }
// Constant-varied twin: entirely different per-row immediates -- the
// exclusion derives from the cross-clone invariance proof, never from
// the constants\' values.
// { dg-final { scan-rtl-dump "Canonicalized counted-row family" "rvtt_replay" } }
// { dg-final { scan-rtl-dump "Formed counted-row record" "rvtt_replay" } }
void varied_row_engine ()
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  auto mean = __builtin_rvtt_sfpreadlreg (4);
  auto m2 = __builtin_rvtt_sfpreadlreg (5);

#define ROW(IMM)						\
  do {								\
    auto t = __builtin_rvtt_sfpmad (x, mean, m2, 0);		\
    auto r = __builtin_rvtt_sfploadi (nullptr, IMM, 0, 0, 0);	\
    mean = __builtin_rvtt_sfpmad (t, r, mean, 0);		\
    m2 = __builtin_rvtt_sfpmad (t, mean, m2, 0);		\
    x = __builtin_rvtt_sfpmad (x, mean, m2, 0);			\
  } while (0)

  ROW (0x1122);
  ROW (0x2233);
  ROW (0x3344);
  ROW (0x4455);
  ROW (0x5566);
  ROW (0x6677);
#undef ROW
  __builtin_rvtt_sfpwritelreg (mean, 4);
  __builtin_rvtt_sfpwritelreg (m2, 5);
}
