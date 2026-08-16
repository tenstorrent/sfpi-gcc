// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -fchecking=2 -mtt-tensix-optimize-invariant-loadi -fdump-tree-rvtt_invariant-details" }
// { dg-final { scan-tree-dump-times "Hoisted invariant SFPU immediate" 5 "rvtt_invariant" } }
// { dg-final { scan-tree-dump-times "left in loop by LREG pressure" 2 "rvtt_invariant" } }
// { dg-final { scan-assembler-times "SFPLOADI" 10 } }

/* Same pressure shape as the generic boundary test, but with a mixture of
   one- and two-issue encodings and an unrelated function name.  The final
   one-issue values are the cheapest deterministic rematerializations.  */
void unrelated_vector_transform ()
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  for (unsigned ix = 0; ix != 8; ++ix)
    {
      auto c0 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e100001, 0, 0, 31);
      x = __builtin_rvtt_sfpmul (x, c0, 0);
      auto c1 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e200002, 0, 0, 31);
      x = __builtin_rvtt_sfpmul (x, c1, 0);
      auto c2 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e300003, 0, 0, 31);
      x = __builtin_rvtt_sfpmul (x, c2, 0);
      auto c3 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e400004, 0, 0, 31);
      x = __builtin_rvtt_sfpmul (x, c3, 0);
      auto c4 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e500000, 0, 0, 31);
      x = __builtin_rvtt_sfpmul (x, c4, 0);
      auto c5 = __builtin_rvtt_sfpxloadi (nullptr, 0x00001234, 0, 0, 31);
      x = __builtin_rvtt_sfpmul (x, c5, 0);
      auto c6 = __builtin_rvtt_sfpxloadi (nullptr, 0x00002345, 0, 0, 31);
      x = __builtin_rvtt_sfpmul (x, c6, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}
