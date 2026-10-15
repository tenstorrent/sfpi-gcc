// MAD-PAIR class fire, negated addend (the Newton reciprocal step
// y * (2 - x * y) as the rdiv body spells it): the hoisted FLOATB
// constant reaches the pair's add through a single-use sign-flipping
// SFPMOV, which the "a + -fp16b" immediate fold consumes just as it
// consumes a plain addend, decaying the row to MUL + ADDI.  Re-claiming
// the constant behind the copy re-offers the pair to the mad combine,
// which folds the sign into the SFPMAD's addend-negate mode.  The second
// function is the renamed, constant-varied twin.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-times "madpair loop bb \\d+ candidate: hoisted constant" 2 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "allocated PRGM L1\\d for constant 0x\[0-9a-f\]+ .madpair class" 2 "rvtt_prgm_const" } }
// { dg-final { scan-assembler-times "SFPMAD\tL\\d+, L\\d+, L\\d+, L1\\d, 2" 2 } }
// { dg-final { scan-assembler-not "SFPADDI" } }

void madpair_negated_fire (void)
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  auto y = __builtin_rvtt_sfpreadlreg (1);
  auto two = __builtin_rvtt_sfpxloadi (nullptr, 0x40000000, 0, 0, 31);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto prod = __builtin_rvtt_sfpmul (x, y, 0);
      auto err = __builtin_rvtt_sfpadd (prod, __builtin_rvtt_sfpmov (two, 1), 0);
      y = __builtin_rvtt_sfpmul (err, y, 0);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc (y, 0);
      __builtin_rvtt_sfppopc (0);
    }
  __builtin_rvtt_sfpwritelreg (y, 1);
}

void renamed_halving_step (void)
{
  auto north = __builtin_rvtt_sfpreadlreg (2);
  auto south = __builtin_rvtt_sfpreadlreg (3);
  auto bias = __builtin_rvtt_sfpxloadi (nullptr, 0x3fc00000, 0, 0, 31);
  for (unsigned row = 0; row != 8; ++row)
    {
      auto scaled = __builtin_rvtt_sfpmul (south, north, 0);
      auto shifted = __builtin_rvtt_sfpadd (scaled, __builtin_rvtt_sfpmov (bias, 1), 0);
      south = __builtin_rvtt_sfpmul (shifted, south, 0);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc (south, 0);
      __builtin_rvtt_sfppopc (0);
    }
  __builtin_rvtt_sfpwritelreg (south, 3);
}
