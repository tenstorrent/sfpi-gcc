/* Shared MAD-restructure shapes: an immediate-fold candidate mul/add pair whose product dies into the add.

   Hooks:
     MRB_KERNEL    kernel name
     MRB_ARM_ADDI  define for the addi-arm shape (loadi'd addend after
		   a two-register product); default is the muli arm
		   (loadi'd multiplicand, register addend)
     MRB_MULTIUSE  define to add a second consumer of the product
		   (the pair is then not single-use)
     MRB_PRESSURE  define for the pressure shape: seven extra vector
		   values live across the pair
     MRB_ADDR_MODE load/store addressing mode (BH default 7; WH max 3)  */

#ifndef MRB_ADDR_MODE
#define MRB_ADDR_MODE 7
#endif

void
MRB_KERNEL (void)
{
#ifdef MRB_PRESSURE
  /* Seven values live across the pair; the pair itself reuses two of
     them (k1 as the multiplicand, k0 as the addend), so the real
     max-live is exactly eight at every point (allocatable) while the
     pair-window peak is eight (7 k's + the add's own result).  */
  auto k0 = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, MRB_ADDR_MODE);
  auto k1 = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, MRB_ADDR_MODE);
  auto k2 = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, MRB_ADDR_MODE);
  auto k3 = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, MRB_ADDR_MODE);
  auto k4 = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, MRB_ADDR_MODE);
  auto k5 = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, MRB_ADDR_MODE);
  auto k6 = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, MRB_ADDR_MODE);
  auto cst = __builtin_rvtt_sfploadi (nullptr, 0x3e80, 0, 0, 0);
  auto p = __builtin_rvtt_sfpmul (k1, cst, 0);
  auto r = __builtin_rvtt_sfpadd (p, k0, 0);
  __builtin_rvtt_sfpstore (nullptr, r, 0, 0, 0, 6, MRB_ADDR_MODE);
  __builtin_rvtt_sfpstore (nullptr, k0, 0, 0, 0, 6, MRB_ADDR_MODE);
  __builtin_rvtt_sfpstore (nullptr, k1, 0, 0, 0, 6, MRB_ADDR_MODE);
  __builtin_rvtt_sfpstore (nullptr, k2, 0, 0, 0, 6, MRB_ADDR_MODE);
  __builtin_rvtt_sfpstore (nullptr, k3, 0, 0, 0, 6, MRB_ADDR_MODE);
  __builtin_rvtt_sfpstore (nullptr, k4, 0, 0, 0, 6, MRB_ADDR_MODE);
  __builtin_rvtt_sfpstore (nullptr, k5, 0, 0, 0, 6, MRB_ADDR_MODE);
  __builtin_rvtt_sfpstore (nullptr, k6, 0, 0, 0, 6, MRB_ADDR_MODE);
}
#else
#ifdef MRB_ARM_ADDI
  auto x = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, MRB_ADDR_MODE);
  auto y = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, MRB_ADDR_MODE);
  auto cst = __builtin_rvtt_sfploadi (nullptr, 0x3fc0, 0, 0, 0);
  auto p = __builtin_rvtt_sfpmul (x, y, 0);
#ifdef MRB_MULTIUSE
  __builtin_rvtt_sfpstore (nullptr, p, 0, 0, 0, 6, MRB_ADDR_MODE);
#endif
  auto r = __builtin_rvtt_sfpadd (p, cst, 0);
#else
  auto x = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, MRB_ADDR_MODE);
  auto c = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, MRB_ADDR_MODE);
  auto cst = __builtin_rvtt_sfploadi (nullptr, 0x3e80, 0, 0, 0);
  auto p = __builtin_rvtt_sfpmul (x, cst, 0);
#ifdef MRB_MULTIUSE
  __builtin_rvtt_sfpstore (nullptr, p, 0, 0, 0, 6, MRB_ADDR_MODE);
#endif
  auto r = __builtin_rvtt_sfpadd (p, c, 0);
#endif
  __builtin_rvtt_sfpstore (nullptr, r, 0, 0, 0, 6, MRB_ADDR_MODE);
}
#endif /* !MRB_PRESSURE */
