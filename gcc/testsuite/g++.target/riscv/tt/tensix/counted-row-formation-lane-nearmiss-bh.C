// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-replay-hoist -mtt-tensix-optimize-counted-row-formation -fdump-rtl-rvtt_replay" }
// Lane-state near miss: a CC write between the rows makes the lane state
// non-constant over any window spanning it, so every family crossing the
// CC region refuses BY NAME (register rewrites are lane-exact only under
// state constancy); rows on one side of the region may still form.
// { dg-final { scan-rtl-dump "counted-row-lane-state: CC write" "rvtt_replay" } }
void lane_nearmiss ()
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

  ROW (0x3f00);
  ROW (0x3e80);
  ROW (0x3e2a);
  __builtin_rvtt_sfppushc (0);
  __builtin_rvtt_sfpsetcc_v (x, 0);
  x = __builtin_rvtt_sfpmad (x, mean, m2, 0);
  __builtin_rvtt_sfppopc (0);
  ROW (0x3e00);
  ROW (0x3dcc);
  ROW (0x3daa);
#undef ROW
  __builtin_rvtt_sfpwritelreg (mean, 4);
  __builtin_rvtt_sfpwritelreg (m2, 5);
}
