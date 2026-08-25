// Near miss of mop-form-deep-preheader-bh.C: CFG depth must not turn a
// runtime bound into a constant trip count.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fwhole-program -fkeep-static-functions -fno-tree-loop-optimize -fno-unroll-loops -mtt-tensix-optimize-mop-form -mtt-tensix-mop-form-force -fdump-rtl-rvtt_mop_form-details" }
// { dg-final { scan-rtl-dump-times "MOP-form refused \\(mop-trips-unproved\\).*trip count is not provably constant" 1 "rvtt_mop_form" } }
// { dg-final { scan-rtl-dump-not "MOP formed" "rvtt_mop_form" } }

extern volatile unsigned preheader_gate;

void deep_preheader_mop_runtime (unsigned n)
{
  __builtin_rvtt_ttreplay (nullptr, 3, 0, 0, 0, 1, 1);
  auto x = __builtin_rvtt_sfpreadlreg (0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  __builtin_rvtt_sfpwritelreg (x, 0);
  unsigned i = 0;
  if (preheader_gate == i) return;
  if (preheader_gate == i) return;
  if (preheader_gate == i) return;
  if (preheader_gate == i) return;
  if (preheader_gate == i) return;
  if (preheader_gate == i) return;
  for (; i != n; ++i)
    __builtin_rvtt_ttreplay (nullptr, 3, 0, 0, 0, 0, 0);
}
