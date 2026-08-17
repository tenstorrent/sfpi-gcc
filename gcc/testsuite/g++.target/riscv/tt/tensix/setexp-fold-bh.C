// setexp-fold: sfpsetexp_v (man, sfpexexp (z, NODEBIAS), LREG)
// -> sfpsetexp_v (man, z, LREG_CPY); the exexp dies when single-use.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-setexp-fold" }

void fold_direct () {
  auto z = __builtin_rvtt_sfpreadlreg (0);
  auto man = __builtin_rvtt_sfpreadlreg (1);
  auto e = __builtin_rvtt_sfpexexp (z, 1);
  auto r = __builtin_rvtt_sfpsetexp_v (man, e, 0);
  __builtin_rvtt_sfpwritelreg (r, 0);
}

// Renamed/varied twin: different registers, the mantissa source is computed,
// and the biased exponent comes from that computed value.
void fold_renamed_varied () {
  auto alpha = __builtin_rvtt_sfpreadlreg (2);
  auto beta = __builtin_rvtt_sfpreadlreg (3);
  auto gamma = __builtin_rvtt_sfpmul (alpha, beta, 0);
  auto raw_exp = __builtin_rvtt_sfpexexp (gamma, 1);
  auto recombined = __builtin_rvtt_sfpsetexp_v (beta, raw_exp, 0);
  __builtin_rvtt_sfpwritelreg (recombined, 1);
}

// Fire with other uses of the exexp result: the setexp still folds to the
// copy form, the exexp survives for its other consumer.
void fold_otheruse () {
  auto z = __builtin_rvtt_sfpreadlreg (0);
  auto man = __builtin_rvtt_sfpreadlreg (1);
  auto e = __builtin_rvtt_sfpexexp (z, 1);
  auto r = __builtin_rvtt_sfpsetexp_v (man, e, 0);
  __builtin_rvtt_sfpwritelreg (r, 0);
  __builtin_rvtt_sfpwritelreg (e, 2);
}

// { dg-final { scan-assembler-times {SFPSETEXP\tL[0-9]+, L[0-9]+, 0, 2} 3 } }
// { dg-final { scan-assembler-times {SFPEXEXP} 1 } }
