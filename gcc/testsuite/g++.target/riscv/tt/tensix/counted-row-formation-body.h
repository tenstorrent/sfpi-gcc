/* Template-expanded "rows": an accumulator pair updated by four mads per
   row, with a per-row immediate materialization mid-row.  The rows repeat
   only MODULO the immediates and the allocator's register rotation --
   word-exact replay formation cannot touch this; the counted-row
   canonicalization (immediate exclusion + clone register rewriting) forms
   one parameterized row program.  */

#ifndef ROW_IMM_BASE
#define ROW_IMM_BASE 0x3f000000
#endif
void counted_row_engine ()
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
