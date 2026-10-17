/* Shapes whose only independent ready instruction sits deeper than the
   one-slot latency-fill window, for the shadow-fill tests.  */

/* QSR's sfpsetcc and sfpswap carry an operand-type argument the other
   architectures do not have; 0 is IMM_TYPE_INT.  Spelled as a token macro
   because some of these calls sit inside backslash-continued macro
   bodies.  */
#ifndef XTT_IMM_TYPE
#if __riscv_xtttensixqsr
#define XTT_IMM_TYPE , 0
#else
#define XTT_IMM_TYPE
#endif
#endif

/* The dependent consumer is a swap (dynamic-delay erratum class), so
   without a filler the bubble is padded with an SFPNOP.  The second
   function varies the producer opcode, immediates and registers.  */

void shadow_fill_deep_swap ()
{
  auto a = __builtin_rvtt_sfpreadlreg (0);
  auto b = __builtin_rvtt_sfpreadlreg (1);
  auto c = __builtin_rvtt_sfpreadlreg (2);
  auto p = __builtin_rvtt_sfpmul (a, a, 0);
  auto r = __builtin_rvtt_sfpswap (p, b, 1 XTT_IMM_TYPE);
  auto p1 = __builtin_rvtt_sfpselect2 (r, 0);
  auto b1 = __builtin_rvtt_sfpselect2 (r, 1);
  auto f = __builtin_rvtt_sfpmul (c, c, 0);
  __builtin_rvtt_sfpwritelreg (p1, 0);
  __builtin_rvtt_sfpwritelreg (b1, 1);
  __builtin_rvtt_sfpwritelreg (f, 2);
}

void renamed_scaled_exchange ()
{
  auto north = __builtin_rvtt_sfpreadlreg (4);
  auto south = __builtin_rvtt_sfpreadlreg (5);
  auto east = __builtin_rvtt_sfpreadlreg (6);
  auto scaled = __builtin_rvtt_sfpmuli (nullptr, north, 0x3f81, 0, 0, 0);
  auto pair = __builtin_rvtt_sfpswap (scaled, south, 1 XTT_IMM_TYPE);
  auto lo = __builtin_rvtt_sfpselect2 (pair, 0);
  auto hi = __builtin_rvtt_sfpselect2 (pair, 1);
  auto other = __builtin_rvtt_sfpadd (east, east, 0);
  __builtin_rvtt_sfpwritelreg (lo, 4);
  __builtin_rvtt_sfpwritelreg (hi, 5);
  __builtin_rvtt_sfpwritelreg (other, 6);
}
