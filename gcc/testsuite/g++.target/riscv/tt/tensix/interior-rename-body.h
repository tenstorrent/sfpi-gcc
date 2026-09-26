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

/* Parameterized barrier-chopped Dst row for the R1 cyclic-interior
   rename consumer: the pushc/setcc CC words chop the row so the SFPU
   payload between them is an INTERIOR region (the load ahead and the
   popc/store/incrwc behind own the seam).  Inside the region the
   allocator packs the short lifetimes into the load's register --
   the storage collisions the consumer requests chain renames for
   through the du-chain rename engine service -- and the composed candidate commits
   on a strict whole-row steady-state II decrease.  */
void IRN_FN ()
{
  for (int d = 0; d < 32; ++d)
    {
      auto v = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc (v, 0 XTT_IMM_TYPE);
      auto t1 = __builtin_rvtt_sfpmul (v, v, 0);
      auto t2 = __builtin_rvtt_sfpmul (t1, t1, 0);
      auto u1 = __builtin_rvtt_sfpmad (v, v, v, 0);
      auto w  = __builtin_rvtt_sfpmad (t2, u1, v, 0);
      __builtin_rvtt_sfppopc (0);
      __builtin_rvtt_sfpstore (nullptr, w, 0, 0, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
