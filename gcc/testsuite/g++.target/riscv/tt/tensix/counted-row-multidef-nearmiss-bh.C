// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-replay-hoist -mtt-tensix-optimize-counted-row-formation -fdump-rtl-rvtt_replay" }
// Multi-definition near miss: a two-register SFPSWAP inside each row has
// no single canonical seed definition register, so every family carrying
// it refuses BY NAME (previously an ICE: seed_def_reg asserted
// single-def while crf_scan_block deliberately models multi-def
// positions).  The swap-free control rows in the second function still
// canonicalize and form.
// { dg-final { scan-rtl-dump "counted-row-multidef-member" "rvtt_replay" } }
// { dg-final { scan-rtl-dump "Formed counted-row record" "rvtt_replay" } }

void multidef_nearmiss ()
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  auto mean = __builtin_rvtt_sfpreadlreg (4);
  auto m2 = __builtin_rvtt_sfpreadlreg (5);

#define ROW(IMM)						\
  do {								\
    auto t = __builtin_rvtt_sfpmad (x, mean, m2, 0);		\
    auto r = __builtin_rvtt_sfploadi (nullptr, IMM, 0, 0, 0);	\
    auto pair = __builtin_rvtt_sfpswap (t, r, 1);		\
    auto lo = __builtin_rvtt_sfpselect2 (pair, 0);		\
    mean = __builtin_rvtt_sfpmad (lo, r, mean, 0);		\
    m2 = __builtin_rvtt_sfpmad (lo, mean, m2, 0);		\
    x = __builtin_rvtt_sfpmad (x, mean, m2, 0);			\
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

// Control: the same shape without the swap forms (single-def members
// only) -- proves the refusal keys on the multi-def member, not on the
// shape or the constants.
void multidef_control ()
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
  ROW (0x3e00);
  ROW (0x3dcc);
  ROW (0x3daa);
#undef ROW
  __builtin_rvtt_sfpwritelreg (mean, 4);
  __builtin_rvtt_sfpwritelreg (m2, 5);
}
