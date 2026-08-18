// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-replay-hoist -mtt-tensix-optimize-counted-row-formation -fdump-rtl-rvtt_replay" }
// Renamed-equivalent twin: identical shape under fresh names -- the
// decision is shape-derived, never name-derived.
// { dg-final { scan-rtl-dump "Canonicalized counted-row family" "rvtt_replay" } }
// { dg-final { scan-rtl-dump "Formed counted-row record" "rvtt_replay" } }
void renamed_row_engine ()
{
  auto sample = __builtin_rvtt_sfpreadlreg (0);
  auto center = __builtin_rvtt_sfpreadlreg (4);
  auto spread = __builtin_rvtt_sfpreadlreg (5);

#define STEP(IMM)							\
  do {									\
    auto delta = __builtin_rvtt_sfpmad (sample, center, spread, 0);	\
    auto scale = __builtin_rvtt_sfploadi (nullptr, IMM, 0, 0, 0);	\
    center = __builtin_rvtt_sfpmad (delta, scale, center, 0);		\
    spread = __builtin_rvtt_sfpmad (delta, center, spread, 0);		\
    sample = __builtin_rvtt_sfpmad (sample, center, spread, 0);		\
  } while (0)

  STEP (0x3f00);
  STEP (0x3e80);
  STEP (0x3e2a);
  STEP (0x3e00);
  STEP (0x3dcc);
  STEP (0x3daa);
#undef STEP
  __builtin_rvtt_sfpwritelreg (center, 4);
  __builtin_rvtt_sfpwritelreg (spread, 5);
}
