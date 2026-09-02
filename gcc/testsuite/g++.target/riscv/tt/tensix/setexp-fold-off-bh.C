// setexp-fold OFF path: with -mno-tt-tensix-optimize-setexp-fold the
// idiom must be left exactly as written.
// Default-ON promotion (hardware-validated: exp/sdpa capture shortening):
// this test used to rely on the ambient default being off; it now pins
// the -mno- spelling so the disabled path stays covered.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mno-tt-tensix-optimize-setexp-fold" }

void no_fold_by_default () {
  auto z = __builtin_rvtt_sfpreadlreg (0);
  auto man = __builtin_rvtt_sfpreadlreg (1);
  auto e = __builtin_rvtt_sfpexexp (z, 1);
  auto r = __builtin_rvtt_sfpsetexp_v (man, e, 0);
  __builtin_rvtt_sfpwritelreg (r, 0);
}

// { dg-final { scan-assembler-times {SFPEXEXP} 1 } }
// { dg-final { scan-assembler-times {SFPSETEXP\tL[0-9]+, L[0-9]+, 0, 0} 1 } }
// { dg-final { scan-assembler-not {SFPSETEXP\tL[0-9]+, L[0-9]+, 0, 2} } }
