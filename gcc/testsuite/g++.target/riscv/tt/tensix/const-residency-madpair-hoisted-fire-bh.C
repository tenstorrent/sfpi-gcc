// MAD-PAIR class fire (lane GA, FX-F1 hardsigmoid shape): the pair's
// constants sit OUTSIDE the loop (where the invariant pass's
// cc-restore-discharged hoist parks them), so neither the in-loop
// residency scan nor the fusion class sees them, and the downstream
// addi immediate fold would consume the shortened FLOATB add constant
// "in preference to mul,add->mad" -- decaying the row body to a
// per-iteration MUL+ADDI.  The class re-claims exactly the
// fold-vulnerable FLOATB constant into a PRGM register in place at the
// hoisted materialization; the constant-register read is not an
// SFPLOADI, the fold no longer matches, and the pre-existing mad
// combine fuses the pair.  The chain-form mul constant (low bits set,
// not FLOATB-representable) is NOT vulnerable and stays in a plain
// LREG: the mad rule fuses register operands where they are.
// RECOGNITION-ONLY: no sfpmad appears in this pass's own gimple
// output.  The loop body carries a lowered v_if region ending in the
// all-lanes SFPENCC (the fresh-body kernel shape the regression was
// bisected on).  The second function is the renamed, constant-varied
// twin.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-times "madpair loop bb \\d+ candidate: hoisted constant" 2 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "allocated PRGM L1\\d for constant 0x\[0-9a-f\]+ .madpair class" 2 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "sfpmad" "rvtt_prgm_const" } }
// { dg-final { scan-assembler-times "SFPMAD" 2 } }
// { dg-final { scan-assembler-times "SFPCONFIG" 2 } }
// { dg-final { scan-assembler-not "SFPADDI" } }

void madpair_hoisted_fire (void)
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  auto gain = __builtin_rvtt_sfpxloadi (nullptr, 0x3e2aaaab, 0, 0, 31);
  auto half = __builtin_rvtt_sfpxloadi (nullptr, 0x3f000000, 0, 0, 31);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto prod = __builtin_rvtt_sfpmul (x, gain, 0);
      x = __builtin_rvtt_sfpadd (prod, half, 0);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc_v (x, 0);
      __builtin_rvtt_sfppopc (0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}

void renamed_saturating_rows (void)
{
  auto acc = __builtin_rvtt_sfpreadlreg (2);
  auto slope = __builtin_rvtt_sfpxloadi (nullptr, 0x3ea8f5c3, 0, 0, 31);
  auto lift = __builtin_rvtt_sfpxloadi (nullptr, 0x40a00000, 0, 0, 31);
  for (unsigned row = 0; row != 8; ++row)
    {
      auto scaled = __builtin_rvtt_sfpmul (acc, slope, 0);
      acc = __builtin_rvtt_sfpadd (scaled, lift, 0);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc_v (acc, 0);
      __builtin_rvtt_sfppopc (0);
    }
  __builtin_rvtt_sfpwritelreg (acc, 2);
}
