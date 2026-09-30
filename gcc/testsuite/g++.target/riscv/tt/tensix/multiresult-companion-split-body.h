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

// Shared body for the companion-split near-miss pair (TOP3-2 layer 2).
// A repeated plain-SFPSWAP group that replay formation would capture and
// execute: two identical four-word instances in one block.  The plain
// swap writes the value bank (L0/L1) with no typed companion results, so
// under possibly-enabled index tracking a capture of it would split the
// value movement from the companion (L4-L7) movement that
// LaneConfig.ENABLE_DEST_INDEX couples to it.

static inline void
repeated_plain_swap_groups ()
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  auto y = __builtin_rvtt_sfpreadlreg (1);

  auto p = __builtin_rvtt_sfpswap (x, y, 1 XTT_IMM_TYPE);
  x = __builtin_rvtt_sfpselect2 (p, 0);
  y = __builtin_rvtt_sfpselect2 (p, 1);
  x = __builtin_rvtt_sfpmul (x, y, 0);
  y = __builtin_rvtt_sfpmul (y, y, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);

  p = __builtin_rvtt_sfpswap (x, y, 1 XTT_IMM_TYPE);
  x = __builtin_rvtt_sfpselect2 (p, 0);
  y = __builtin_rvtt_sfpselect2 (p, 1);
  x = __builtin_rvtt_sfpmul (x, y, 0);
  y = __builtin_rvtt_sfpmul (y, y, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);

  __builtin_rvtt_sfpwritelreg (x, 0);
  __builtin_rvtt_sfpwritelreg (y, 1);
}
