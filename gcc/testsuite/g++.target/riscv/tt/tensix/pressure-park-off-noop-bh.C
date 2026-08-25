// PRESSURE-PARK knob-off twin (lane GV): the exact
// pressure-park-lreg-fire-bh.C body WITHOUT
// -mtt-tensix-optimize-pressure-park.  All four materializations sit
// after the body's first CC writer, so the established peel-class
// position rule stops the candidate scan and nothing is admitted,
// programmed, peeled, or hoisted -- the widened machinery contributes
// no text at all when the knob is off.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-not "pressure-park:" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "allocated PRGM" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "peeled first iteration" "rvtt_prgm_const" } }
// { dg-final { scan-assembler-not "SFPCONFIG" } }

void park_off_noop (void)
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc_v (x, 0);
      __builtin_rvtt_sfppopc (0);
      auto c0 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b1a3d, 0, 0, 31);
      auto c1 = __builtin_rvtt_sfpxloadi (nullptr, 0x3f21aa52, 0, 0, 31);
      auto c2 = __builtin_rvtt_sfpxloadi (nullptr, 0x40490fdb, 0, 0, 31);
      auto c3 = __builtin_rvtt_sfpxloadi (nullptr, 0x402df854, 0, 0, 31);
      x = __builtin_rvtt_sfpmul (x, c0, 0);
      x = __builtin_rvtt_sfpmul (x, c1, 0);
      x = __builtin_rvtt_sfpmul (x, c2, 0);
      x = __builtin_rvtt_sfpmul (x, c3, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}
