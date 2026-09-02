// ICE reproducer: this exact shape
// made the reference compiler crash at assembly output with
// "internal compiler error: cannot store sfpu register (register
// spill)".  Under the remat flag it must simply compile.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-remat" }
// { dg-final { scan-assembler "SFPMAD" } }
// { dg-final { scan-assembler-not "BADSTORE" } }

void ice_pressure (void)
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  auto c0 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b1a3d, 0, 0, 31);
  auto c1 = __builtin_rvtt_sfpxloadi (nullptr, 0xbf91c2e7, 0, 0, 31);
  auto c2 = __builtin_rvtt_sfpxloadi (nullptr, 0x3f000000, 0, 0, 31);
  auto c3 = __builtin_rvtt_sfpxloadi (nullptr, 0x40490fdb, 0, 0, 31);
  auto c4 = __builtin_rvtt_sfpxloadi (nullptr, 0x3ea2e8ba, 0, 0, 31);
  auto c5 = __builtin_rvtt_sfpxloadi (nullptr, 0xbe000000, 0, 0, 31);
  auto c6 = __builtin_rvtt_sfpxloadi (nullptr, 0x3f317218, 0, 0, 31);
  auto c7 = __builtin_rvtt_sfpxloadi (nullptr, 0x3fb8aa3b, 0, 0, 31);
  auto c8 = __builtin_rvtt_sfpxloadi (nullptr, 0xbf5f4dad, 0, 0, 31);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      x = __builtin_rvtt_sfpmad (x, c0, c1, 0);
      x = __builtin_rvtt_sfpmad (x, c2, c3, 0);
      x = __builtin_rvtt_sfpmad (x, c4, c5, 0);
      x = __builtin_rvtt_sfpmad (x, c6, c7, 0);
      x = __builtin_rvtt_sfpmad (x, c8, c0, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}
