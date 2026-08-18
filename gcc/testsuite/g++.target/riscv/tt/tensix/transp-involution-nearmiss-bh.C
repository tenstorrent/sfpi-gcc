// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-transp-involution -fdump-tree-rvtt_transp_involution" }
// Near misses, each refusing by name and leaving the single-transpose
// lowering and the parks in place.
// 1: one transpose operand is computed (not a full-bank load) ->
//    transp-involution-operand-shape.
// 2: mixed gather formats -> transp-involution-operand-shape.
// 3: a non-no-increment address mode -> transp-involution-operand-shape.
// 4: a Dst store between the gather loads and the transpose ->
//    transp-involution-window.
// { dg-final { scan-assembler-times "SFPTRANSP" 4 } }
// { dg-final { scan-assembler-not "SFPENCC" } }
// { dg-final { scan-tree-dump-times "transp-involution-operand-shape" 3 "rvtt_transp_involution" } }
// { dg-final { scan-tree-dump-times "transp-involution-window" 1 "rvtt_transp_involution" } }
using sfpu_t = decltype (__builtin_rvtt_sfpreadlreg (9));

void computed_operand ()
{
  auto acc = __builtin_rvtt_sfpreadlreg (9);
  __builtin_rvtt_sfpstore (nullptr, acc, 320, 0, 0, 3, 7);
  auto x0 = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
  auto x1 = __builtin_rvtt_sfpload (nullptr, 2, 0, 0, 0, 7);
  auto x2 = __builtin_rvtt_sfpload (nullptr, 16, 0, 0, 0, 7);
  auto x3 = __builtin_rvtt_sfpmul (x0, x1, 0);	// computed, not a load
  auto r = __builtin_rvtt_sfptransp (x0, x1, x2, x3);
  x0 = __builtin_rvtt_sfpselect4 (r, 0);
  acc = __builtin_rvtt_sfpload (nullptr, 320, 0, 0, 3, 7);
  acc = __builtin_rvtt_sfpmad (x0, x0, acc, 0);
  __builtin_rvtt_sfpstore (nullptr, acc, 320, 0, 0, 3, 7);
}

void mixed_formats ()
{
  auto acc = __builtin_rvtt_sfpreadlreg (9);
  __builtin_rvtt_sfpstore (nullptr, acc, 320, 0, 0, 3, 7);
  auto x0 = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
  auto x1 = __builtin_rvtt_sfpload (nullptr, 2, 0, 0, 0, 7);
  auto x2 = __builtin_rvtt_sfpload (nullptr, 16, 0, 0, 0, 7);
  auto x3 = __builtin_rvtt_sfpload (nullptr, 18, 0, 0, 3, 7);	// FP32 vs SRCB
  auto r = __builtin_rvtt_sfptransp (x0, x1, x2, x3);
  x0 = __builtin_rvtt_sfpselect4 (r, 0);
  acc = __builtin_rvtt_sfpload (nullptr, 320, 0, 0, 3, 7);
  acc = __builtin_rvtt_sfpmad (x0, x0, acc, 0);
  __builtin_rvtt_sfpstore (nullptr, acc, 320, 0, 0, 3, 7);
}

void stepping_mode ()
{
  auto acc = __builtin_rvtt_sfpreadlreg (9);
  __builtin_rvtt_sfpstore (nullptr, acc, 320, 0, 0, 3, 7);
  auto x0 = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 4);	// mode 4 != no-inc
  auto x1 = __builtin_rvtt_sfpload (nullptr, 2, 0, 0, 0, 4);
  auto x2 = __builtin_rvtt_sfpload (nullptr, 16, 0, 0, 0, 4);
  auto x3 = __builtin_rvtt_sfpload (nullptr, 18, 0, 0, 0, 4);
  auto r = __builtin_rvtt_sfptransp (x0, x1, x2, x3);
  x0 = __builtin_rvtt_sfpselect4 (r, 0);
  acc = __builtin_rvtt_sfpload (nullptr, 320, 0, 0, 3, 7);
  acc = __builtin_rvtt_sfpmad (x0, x0, acc, 0);
  __builtin_rvtt_sfpstore (nullptr, acc, 320, 0, 0, 3, 7);
}

void store_in_window ()
{
  auto acc = __builtin_rvtt_sfpreadlreg (9);
  __builtin_rvtt_sfpstore (nullptr, acc, 320, 0, 0, 3, 7);
  auto x0 = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
  auto x1 = __builtin_rvtt_sfpload (nullptr, 2, 0, 0, 0, 7);
  auto x2 = __builtin_rvtt_sfpload (nullptr, 16, 0, 0, 0, 7);
  auto x3 = __builtin_rvtt_sfpload (nullptr, 18, 0, 0, 0, 7);
  __builtin_rvtt_sfpstore (nullptr, acc, 448, 0, 0, 3, 7);	// window barrier
  auto r = __builtin_rvtt_sfptransp (x0, x1, x2, x3);
  x0 = __builtin_rvtt_sfpselect4 (r, 0);
  acc = __builtin_rvtt_sfpload (nullptr, 320, 0, 0, 3, 7);
  acc = __builtin_rvtt_sfpmad (x0, x0, acc, 0);
  __builtin_rvtt_sfpstore (nullptr, acc, 320, 0, 0, 3, 7);
}
