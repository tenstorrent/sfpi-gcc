// Peel-class near miss: a REAL branch in the loop body (multi-block)
// defeats the CC-canonical proof -- program order is no longer the
// unique execution order, so the linear canonical-tail argument does
// not apply and the plain sfpu-barrier refusal stands byte-identically.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump "cc-canonical proof failed .multi-block-body." "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump "loop bb \\d+ refused .sfpu-barrier." "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "peeled first iteration" "rvtt_prgm_const" } }
// { dg-final { scan-assembler-not "SFPCONFIG" } }

void peel_multiblock (const unsigned *sel)
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto gain = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b1a3d, 0, 0, 31);
      x = __builtin_rvtt_sfpmul (x, gain, 0);
      if (sel[ix] & 1)
	x = __builtin_rvtt_sfpmul (x, x, 0);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc_v (x, 0);
      __builtin_rvtt_sfppopc (0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}
