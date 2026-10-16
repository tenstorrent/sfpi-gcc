// Check that a cross-lane SFPTRANSP use refuses rematerialization and reports the spill error.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-remat -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump "const-remat: use refused .consumer-lane-discipline-unaudited." "rvtt_prgm_const" } }
// { dg-error "lreg-pressure-exceeded" "" { target *-*-* } 0 }
// { dg-message "proven-constant values" "" { target *-*-* } 0 }

void remat_consumer_nearmiss (void)
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  auto c0 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0000, 0, 0, 31);
  auto c1 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0001, 0, 0, 31);
  auto c2 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0002, 0, 0, 31);
  auto c3 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0003, 0, 0, 31);
  auto c4 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0004, 0, 0, 31);
  auto c5 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0005, 0, 0, 31);
  auto c6 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0006, 0, 0, 31);
  auto c7 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0007, 0, 0, 31);
  auto c8 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0008, 0, 0, 31);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      x = __builtin_rvtt_sfpmad (x, c0, c1, 0);
      x = __builtin_rvtt_sfpmad (x, c2, c3, 0);
      x = __builtin_rvtt_sfpmad (x, c4, c5, 0);
      x = __builtin_rvtt_sfpmad (x, c6, c7, 0);
      /* The cross-lane transpose is the only use of c8.  */
      auto t = __builtin_rvtt_sfptransp (c8, c0, c1, c2,
                                         __builtin_rvtt_sfpreadlreg (9), __builtin_rvtt_sfpreadlreg (9), __builtin_rvtt_sfpreadlreg (9), __builtin_rvtt_sfpreadlreg (9));
      auto y = __builtin_rvtt_sfpselect8 (t, 0);
      x = __builtin_rvtt_sfpmad (x, y, c0, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}
