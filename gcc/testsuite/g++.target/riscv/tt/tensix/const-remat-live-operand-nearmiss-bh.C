// Live-operand near miss: the ninth value's only use is the LIVE
// (destination-tied) operand of an _lv consumer -- the consumer's
// CC-disabled result lanes ARE that operand's lanes (rvtt.md _lv
// alternatives constrain it "0"), so a clone that wrote only the
// enabled lanes would leak garbage through the merge.  The use refuses
// and the residual over-pressure is the named spill error, never
// silent wrong code.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-remat -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump "const-remat: use refused .consumer-lane-discipline-unaudited." "rvtt_prgm_const" } }
// { dg-error "lreg-pressure-exceeded" "" { target *-*-* } 0 }

void remat_live_operand_nearmiss (void)
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  /* Defined first so the remat driver reaches it before the pressure
     model is satisfied by the later candidates.  */
  auto c8 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0008, 0, 0, 31);
  auto c0 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0000, 0, 0, 31);
  auto c1 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0001, 0, 0, 31);
  auto c2 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0002, 0, 0, 31);
  auto c3 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0003, 0, 0, 31);
  auto c4 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0004, 0, 0, 31);
  auto c5 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0005, 0, 0, 31);
  auto c6 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0006, 0, 0, 31);
  auto c7 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0007, 0, 0, 31);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      x = __builtin_rvtt_sfpmad (x, c0, c1, 0);
      x = __builtin_rvtt_sfpmad (x, c2, c3, 0);
      x = __builtin_rvtt_sfpmad (x, c4, c5, 0);
      x = __builtin_rvtt_sfpmad (x, c6, c7, 0);
      /* c8's only use is the live (merge) operand.  */
      x = __builtin_rvtt_sfpmad_lv (c8, x, c0, c1, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}
