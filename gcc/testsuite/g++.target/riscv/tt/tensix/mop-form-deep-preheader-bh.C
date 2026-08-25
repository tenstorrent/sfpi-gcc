// Six non-opaque preheader blocks must not interfere with MOP formation.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fwhole-program -fkeep-static-functions -fno-tree-loop-optimize -fno-unroll-loops -mtt-tensix-optimize-mop-form -fdump-rtl-rvtt_mop_form-details" }
// { dg-final { scan-rtl-dump-times "MOP formed \\(mop0-lA-replay, counted loop\\): 20 iterations" 1 "rvtt_mop_form" } }
// { dg-final { scan-assembler-times "TTMOP\\t0, 19, 0" 1 } }

extern volatile unsigned preheader_gate;

void deep_preheader_mop ()
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
  for (; i != 20; ++i)
    __builtin_rvtt_ttreplay (nullptr, 3, 0, 0, 0, 0, 0);
}
