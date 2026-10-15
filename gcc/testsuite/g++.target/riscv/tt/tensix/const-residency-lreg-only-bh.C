// The LREG tier ahead of PRGM for constants whose consumers cannot read a
// constant register in place.  A LUT table slot reads a fixed LREG, and a
// compare's VD of L12 or above suppresses the compare, so a PRGM park
// would be copied back into an LREG on every iteration.  All six table
// coefficients stay resident next to the input and the result (exactly
// the eight-LREG file, measured rather than charged one register per
// park), and the compare's lower bound is parked in an LREG.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-pressure-park -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-times "pressure-park: hoisted invariant materialization to a free LREG" 7 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "lreg-file-exhausted" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "allocated PRGM L1\\d for constant 0x(3\[0-9a-f\]{7}|ff800000)" "rvtt_prgm_const" } }
// { dg-final { scan-assembler-times "SFPLUTFP32\tL7, 7" 1 } }
// { dg-final { scan-assembler-times "SFPLE\tL\[0-7\], L\[0-7\], 0, 1" 1 } }

void lut_tables_resident (void)
{
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto x = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      auto t0 = __builtin_rvtt_sfpxloadi (nullptr, 0x33e5317d, 0, 0, 31);
      auto t1 = __builtin_rvtt_sfpxloadi (nullptr, 0x301f318d, 0, 0, 31);
      auto t2 = __builtin_rvtt_sfpxloadi (nullptr, 0x2a821c85, 0, 0, 31);
      auto t3 = __builtin_rvtt_sfpxloadi (nullptr, 0x23a50000, 0, 0, 31);
      auto t4 = __builtin_rvtt_sfpxloadi (nullptr, 0x2b8e301e, 0, 0, 31);
      auto t5 = __builtin_rvtt_sfpxloadi (nullptr, 0x377d34a3, 0, 0, 31);
      auto r = __builtin_rvtt_sfplutfp32_6r (t0, t1, t2, t3, t4, t5, x, 7);
      __builtin_rvtt_sfpstore (nullptr, __builtin_rvtt_sfpmul (r, x, 0),
			       0, 0, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}

void compare_vd_resident (void)
{
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto x = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      __builtin_rvtt_sfppushc (0);
      auto hi = __builtin_rvtt_sfpxloadi (nullptr, 0x40a00000, 0, 0, 31);
      __builtin_rvtt_sfple (x, hi, 1);
      auto lo = __builtin_rvtt_sfpxloadi (nullptr, 0xff800000, 0, 0, 31);
      __builtin_rvtt_sfple (lo, x, 1);
      __builtin_rvtt_sfpstore (nullptr, __builtin_rvtt_sfpreadlreg (9),
			       0, 0, 0, 0, 7);
      __builtin_rvtt_sfppopc (0);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
