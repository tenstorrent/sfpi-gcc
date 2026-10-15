// PRESSURE-PARK post-CC admission through a native compare: a constant
// read as the VC operand of the SET_CC compare form (SFPLE/SFPGT mod1 1,
// the threshold body's "x <= -inf" test) is a lane-local read whose only
// write is LaneFlags in enabled lanes, so the every-lane constant-register
// read is exact on every lane the original read.  The constant parks in a
// PRGM register instead of being re-materialized each row.  The second
// function is the near miss: the constant is the compare's VD operand,
// which the consumer audit keeps refusing.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-pressure-park -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-times "pressure-park: admitted post-CC candidate" 1 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "pressure-park: refused .consumer-lane-discipline-unaudited." 1 "rvtt_prgm_const" } }
// { dg-final { scan-assembler-times "SFPCONFIG" 1 } }
// { dg-final { scan-assembler "SFPLE\tL\\d+, L1\\d, 0, 1" } }

void compare_vc_fire (void)
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  auto out = __builtin_rvtt_sfpreadlreg (1);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      x = __builtin_rvtt_sfpmul (x, x, 0);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc (x, 0);
      auto floor = __builtin_rvtt_sfpxloadi (nullptr, 0xff800000, 0, 0, 31);
      __builtin_rvtt_sfple (x, floor, 1);
      out = __builtin_rvtt_sfpassign_lv (out, x);
      __builtin_rvtt_sfppopc (0);
    }
  __builtin_rvtt_sfpwritelreg (out, 1);
}

void compare_vd_refuse (void)
{
  auto y = __builtin_rvtt_sfpreadlreg (2);
  auto res = __builtin_rvtt_sfpreadlreg (3);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      y = __builtin_rvtt_sfpmul (y, y, 0);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc (y, 0);
      auto lim = __builtin_rvtt_sfpxloadi (nullptr, 0xbf000000, 0, 0, 31);
      __builtin_rvtt_sfpgt (lim, y, 1);
      res = __builtin_rvtt_sfpassign_lv (res, y);
      __builtin_rvtt_sfppopc (0);
    }
  __builtin_rvtt_sfpwritelreg (res, 3);
}
