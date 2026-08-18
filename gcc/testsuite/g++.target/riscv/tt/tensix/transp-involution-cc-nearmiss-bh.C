// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-transp-involution -fdump-tree-rvtt_transp_involution" }
// A live typed CC region anywhere in the function defeats the v1
// lane-state discipline: the whole function refuses
// (transp-involution-cc-region), the parked shape survives with its
// single unfused transpose, and no all-lanes enable is materialized (the
// only SFPENCC is the structured-CC region's own exit restore).
// { dg-final { scan-assembler-times "SFPTRANSP" 1 } }
// { dg-final { scan-assembler-times "SFPENCC" 1 } }
// { dg-final { scan-assembler-times "SFPSTORE" 3 } }
// { dg-final { scan-tree-dump "transp-involution-cc-region" "rvtt_transp_involution" } }
void involution_cc_region ()
{
  auto acc = __builtin_rvtt_sfpreadlreg (9);
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
  auto probe = __builtin_rvtt_sfpload (nullptr, 64, 0, 0, 0, 7);
  __builtin_rvtt_sfppushc (0);
  __builtin_rvtt_sfpsetcc_v (probe, 0);
  probe = __builtin_rvtt_sfpmul (probe, probe, 0);
  __builtin_rvtt_sfppopc (0);
  __builtin_rvtt_sfpstore (nullptr, probe, 448, 0, 0, 3, 7);
}
