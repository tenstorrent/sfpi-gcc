/* QSR's sfpsetcc and sfpswap carry an operand-type argument the other
   architectures do not have.  0 is IMM_TYPE_INT, which is what these
   bit-pattern subjects want.  Spelled as a token macro rather than an
   #if around the argument because some of these calls sit inside
   backslash-continued macro bodies.  */
#ifndef XTT_IMM_TYPE
#if __riscv_xtttensixqsr
#define XTT_IMM_TYPE , 0
#else
#define XTT_IMM_TYPE
#endif
#endif

/* P1 reads P0, so it is not an independent ready filler.  */
void
latency_fill_raw_rejected ()
{
  auto a = __builtin_rvtt_sfpreadlreg (0);
  auto b = __builtin_rvtt_sfpreadlreg (1);
  auto c = __builtin_rvtt_sfpreadlreg (2);
  auto d = __builtin_rvtt_sfpreadlreg (3);
  auto p0 = __builtin_rvtt_sfpmul (a, b, 0);
  auto r0 = __builtin_rvtt_sfpmad (p0, c, d, 0);
  auto p1 = __builtin_rvtt_sfpmul (p0, b, 0);
  __builtin_rvtt_sfpwritelreg (r0, 0);
  __builtin_rvtt_sfpwritelreg (p1, 1);
}

/* A CC-mutating instruction is an explicit scheduling barrier.  */
void
latency_fill_cc_rejected ()
{
  auto a = __builtin_rvtt_sfpreadlreg (0);
  auto b = __builtin_rvtt_sfpreadlreg (1);
  auto c = __builtin_rvtt_sfpreadlreg (2);
  auto d = __builtin_rvtt_sfpreadlreg (3);
  auto p0 = __builtin_rvtt_sfpmul (a, b, 0);
  auto r0 = __builtin_rvtt_sfpmad (p0, c, d, 0);
  __builtin_rvtt_sfppushc (0);
  __builtin_rvtt_sfpsetcc (b, 0 XTT_IMM_TYPE);
  __builtin_rvtt_sfppopc (0);
  auto p1 = __builtin_rvtt_sfpmul (c, d, 0);
  __builtin_rvtt_sfpwritelreg (r0, 0);
  __builtin_rvtt_sfpwritelreg (p1, 1);
}
