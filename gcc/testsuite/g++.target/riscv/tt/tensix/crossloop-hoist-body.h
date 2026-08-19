/* Cross-loop hoist test shape: a row loop consuming loop-invariant
   32-bit immediate materializations (the early invariant pass places
   them in the row preheader), inside a tile loop whose body delivers
   audited architectural words -- raw `.ttinsn' RWC programming and
   synchronization plus a runtime-composed thread-config push through
   the instruction-buffer anchor.  Every name and every coefficient
   value is macro-parameterized: the hoist decision must be identical
   under renaming and under different values (nothing may key on
   either).

   Hooks:
     XLH_TILE_EXTRA()	 extra statement inside the tile loop body
     XLH_ROW_EXTRA(x)	 extra statement inside the row loop
     XLH_GUARD_BEGIN/END wrap the row loop (speculation shapes)  */

extern volatile unsigned int __instrn_buffer[];

#ifndef XLH_ADDR_MODE
#define XLH_ADDR_MODE 7		/* BH no-increment; WH tests use 3 */
#endif
#ifndef XLH_TILE_EXTRA
#define XLH_TILE_EXTRA() do {} while (0)
#endif
#ifndef XLH_ROW_EXTRA
#define XLH_ROW_EXTRA(x) do {} while (0)
#endif
#ifndef XLH_GUARD_BEGIN
#define XLH_GUARD_BEGIN
#define XLH_GUARD_END
#endif

void
XLH_KERNEL (int XLH_TILES)
{
  for (int XLH_T = 0; XLH_T != XLH_TILES; ++XLH_T)
    {
      /* The per-tile delivered words: SETRWC (RWC counters only), a
	 sync-family word, and a composed thread-config push whose
	 runtime operand stays inside its bit field (the TT_OP
	 discipline).  */
      __asm__ __volatile__ (".ttinsn %0" : : "n" (0x37000104));
      __asm__ __volatile__ (".ttinsn %0" : : "n" (0xa2820010));
      __instrn_buffer[0] = 0xb2020000u | ((unsigned) XLH_T & 0x1ffu);
      XLH_TILE_EXTRA ();
      XLH_GUARD_BEGIN
      for (int XLH_ROW = 0; XLH_ROW != 8; ++XLH_ROW)
	{
	  auto XLH_C0 = __builtin_rvtt_sfpxloadi (nullptr, XLH_VAL_C0,
						  0, 0, -32);
	  auto XLH_C1 = __builtin_rvtt_sfpxloadi (nullptr, XLH_VAL_C1,
						  0, 0, -32);
	  auto XLH_X = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6,
					       XLH_ADDR_MODE);
	  XLH_X = __builtin_rvtt_sfpmad (XLH_X, XLH_C0, XLH_C1, 0);
	  XLH_ROW_EXTRA (XLH_X);
	  __builtin_rvtt_sfpstore (nullptr, XLH_X, 0, 0, 0, 6,
				   XLH_ADDR_MODE);
	  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
	}
      XLH_GUARD_END
    }
}
