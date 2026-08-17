// setexp-fold on Wormhole: same idiom, same fold.
// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-setexp-fold" }

void fold_direct () {
  auto z = __builtin_rvtt_sfpreadlreg (0);
  auto man = __builtin_rvtt_sfpreadlreg (1);
  auto e = __builtin_rvtt_sfpexexp (z, 1);
  auto r = __builtin_rvtt_sfpsetexp_v (man, e, 0);
  __builtin_rvtt_sfpwritelreg (r, 0);
}

// Renamed/varied twin.
void fold_renamed_varied () {
  auto alpha = __builtin_rvtt_sfpreadlreg (2);
  auto beta = __builtin_rvtt_sfpreadlreg (3);
  auto gamma = __builtin_rvtt_sfpmul (alpha, beta, 0);
  auto raw_exp = __builtin_rvtt_sfpexexp (gamma, 1);
  auto recombined = __builtin_rvtt_sfpsetexp_v (beta, raw_exp, 0);
  __builtin_rvtt_sfpwritelreg (recombined, 1);
}

// { dg-final { scan-assembler-times {SFPSETEXP\tL[0-9]+, L[0-9]+, 0, 2} 2 } }
// { dg-final { scan-assembler-not {SFPEXEXP} } }
