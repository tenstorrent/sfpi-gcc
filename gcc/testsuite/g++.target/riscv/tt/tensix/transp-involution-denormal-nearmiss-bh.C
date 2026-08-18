// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-transp-involution -fdump-tree-rvtt_transp_involution" }
// The bundles form, but an FP32-format park of a value with no audited
// never-denormal producer (a programmable constant register read) keeps its
// store/load pair: the store arm's denormal flush would be observable.
// { dg-final { scan-assembler-times "SFPTRANSP" 2 } }
// { dg-final { scan-tree-dump "transp-park-denormal-unproven" "rvtt_transp_involution" } }
using sfpu_t = decltype (__builtin_rvtt_sfpreadlreg (9));

void denormal_unproven ()
{
  auto acc = __builtin_rvtt_sfpreadlreg (11);	// programmable constant: bits unproven
  __builtin_rvtt_sfpstore (nullptr, acc, 320, 0, 0, 3, 7);
  auto x0 = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
  auto x1 = __builtin_rvtt_sfpload (nullptr, 2, 0, 0, 0, 7);
  auto x2 = __builtin_rvtt_sfpload (nullptr, 16, 0, 0, 0, 7);
  auto x3 = __builtin_rvtt_sfpload (nullptr, 18, 0, 0, 0, 7);
  auto r = __builtin_rvtt_sfptransp (x0, x1, x2, x3);
  x0 = __builtin_rvtt_sfpselect4 (r, 0);
  acc = __builtin_rvtt_sfpload (nullptr, 320, 0, 0, 3, 7);
  acc = __builtin_rvtt_sfpmad (x0, x0, acc, 0);
  __builtin_rvtt_sfpstore (nullptr, acc, 320, 0, 0, 3, 7);
}
