// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-unroll-loops -mno-tt-tensix-optimize-replay -mtt-tensix-optimize-lreg-rename -fdump-rtl-rvtt_lreg_rename-details" }
// Near miss: an opaque assembly statement inside the row kills the
// span (its register contacts are unknowable).  Refuse by name.
// { dg-final { scan-rtl-dump "Lreg rename refused: rename-span-opaque" "rvtt_lreg_rename" } }
// { dg-final { scan-rtl-dump-not "Lreg rename: chain" "rvtt_lreg_rename" } }
void ren_opaque ()
{
  auto k1 = __builtin_rvtt_sfpreadlreg (0);
  auto k2 = __builtin_rvtt_sfpreadlreg (1);
  auto x = __builtin_rvtt_sfpreadlreg (2);
  for (unsigned row = 0; row != 20; ++row)
    {
      auto t = __builtin_rvtt_sfpand (k1, k2);
      auto p = __builtin_rvtt_sfpmul (x, x, 0);
      auto q = __builtin_rvtt_sfpmul (p, p, 0);
      __asm__ __volatile__ ("" ::: "memory");
      auto r = __builtin_rvtt_sfpxor (q, t);
      auto u = __builtin_rvtt_sfpand (k2, k1);
      x = __builtin_rvtt_sfpxor (r, u);
    }
  __builtin_rvtt_sfpwritelreg (x, 2);
}
