// setexp-fold near misses: every function here must keep its SFPEXEXP and
// keep the SFPSETEXP in the exponent-from-int form (mod1 0).
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-setexp-fold" }

// Debiased exponent (mod1 0): not the raw field, value differs by the bias.
void refuse_debias () {
  auto z = __builtin_rvtt_sfpreadlreg (0);
  auto man = __builtin_rvtt_sfpreadlreg (1);
  auto e = __builtin_rvtt_sfpexexp (z, 0);
  auto r = __builtin_rvtt_sfpsetexp_v (man, e, 0);
  __builtin_rvtt_sfpwritelreg (r, 0);
}

// CC-setting exponent extract (mod1 2 = SET_CC_SGN_EXP): the extract's CC
// write must survive, and the CC region changes at the extract.
void refuse_cc_exexp () {
  auto z = __builtin_rvtt_sfpreadlreg (0);
  auto man = __builtin_rvtt_sfpreadlreg (1);
  __builtin_rvtt_sfppushc (0);
  auto e = __builtin_rvtt_sfpexexp (z, 2);
  auto r = __builtin_rvtt_sfpsetexp_v (man, e, 0);
  __builtin_rvtt_sfppopc (0);
  __builtin_rvtt_sfpwritelreg (r, 0);
}

// A CC-writing instruction between the extract and the recombine: the two
// sit in different CC regions, so their lane masks may differ.
void refuse_cc_between () {
  auto z = __builtin_rvtt_sfpreadlreg (0);
  auto man = __builtin_rvtt_sfpreadlreg (1);
  auto e = __builtin_rvtt_sfpexexp (z, 1);
  __builtin_rvtt_sfppushc (0);
  __builtin_rvtt_sfpsetcc_v (man, 0);
  auto r = __builtin_rvtt_sfpsetexp_v (man, e, 0);
  __builtin_rvtt_sfppopc (0);
  __builtin_rvtt_sfpwritelreg (r, 0);
}

// { dg-final { scan-assembler-times {SFPEXEXP} 3 } }
// { dg-final { scan-assembler-times {SFPSETEXP\tL[0-9]+, L[0-9]+, 0, 0} 3 } }
// { dg-final { scan-assembler-not {SFPSETEXP\tL[0-9]+, L[0-9]+, 0, 2} } }
