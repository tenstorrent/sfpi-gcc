// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-replay-hoist -mtt-tensix-optimize-counted-row-formation -fdump-rtl-rvtt_replay" }
// Slot-budget near miss: a user fixed capture owns the WHOLE replay
// buffer, so no span is available and no counted-row record can form --
// the budget never grows by stealing user-recorded slots, and the
// perfectly-formable rows stay inline.
// { dg-final { scan-rtl-dump-not "Canonicalized counted-row" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "Formed counted-row record" "rvtt_replay" } }
// { dg-final { scan-assembler-times "TTREPLAY" 1 } }
#define RAW_WORD(w) __asm__ __volatile__ (".ttinsn %0" :: "n" (w))
void budget_nearmiss ()
{
  __builtin_rvtt_ttreplay (nullptr, 32, 0, 0, 0, 0, 1);
  RAW_WORD (0x8f000000u); RAW_WORD (0x8f000000u); RAW_WORD (0x8f000000u);
  RAW_WORD (0x8f000000u); RAW_WORD (0x8f000000u); RAW_WORD (0x8f000000u);
  RAW_WORD (0x8f000000u); RAW_WORD (0x8f000000u); RAW_WORD (0x8f000000u);
  RAW_WORD (0x8f000000u); RAW_WORD (0x8f000000u); RAW_WORD (0x8f000000u);
  RAW_WORD (0x8f000000u); RAW_WORD (0x8f000000u); RAW_WORD (0x8f000000u);
  RAW_WORD (0x8f000000u); RAW_WORD (0x8f000000u); RAW_WORD (0x8f000000u);
  RAW_WORD (0x8f000000u); RAW_WORD (0x8f000000u); RAW_WORD (0x8f000000u);
  RAW_WORD (0x8f000000u); RAW_WORD (0x8f000000u); RAW_WORD (0x8f000000u);
  RAW_WORD (0x8f000000u); RAW_WORD (0x8f000000u); RAW_WORD (0x8f000000u);
  RAW_WORD (0x8f000000u); RAW_WORD (0x8f000000u); RAW_WORD (0x8f000000u);
  RAW_WORD (0x8f000000u); RAW_WORD (0x8f000000u);

  auto x = __builtin_rvtt_sfpreadlreg (0);
  auto mean = __builtin_rvtt_sfpreadlreg (4);
  auto m2 = __builtin_rvtt_sfpreadlreg (5);

#define ROW(IMM)						\
  do {								\
    auto t = __builtin_rvtt_sfpmad (x, mean, m2, 0);		\
    auto r = __builtin_rvtt_sfploadi (nullptr, IMM, 0, 0, 0);	\
    mean = __builtin_rvtt_sfpmad (t, r, mean, 0);		\
    m2 = __builtin_rvtt_sfpmad (t, mean, m2, 0);		\
    auto u = __builtin_rvtt_sfpmad (t, m2, mean, 0);		\
    x = __builtin_rvtt_sfpmad (u, mean, m2, 0);			\
  } while (0)

  ROW (0x3f00);
  ROW (0x3e80);
  ROW (0x3e2a);
  ROW (0x3e00);
  ROW (0x3dcc);
  ROW (0x3daa);
#undef ROW
  __builtin_rvtt_sfpwritelreg (mean, 4);
  __builtin_rvtt_sfpwritelreg (m2, 5);
}
