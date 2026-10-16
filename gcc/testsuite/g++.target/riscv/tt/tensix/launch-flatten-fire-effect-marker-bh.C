// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-launch-flatten -fdump-tree-rvtt_launch_flatten" }
// Check that raw-LREG effect markers after computed delivery words are priced at zero and the loop is still requested.
// { dg-final { scan-tree-dump "launch-flatten: requested complete unroll of loop \[0-9\]+ \\(~\[0-9\]+ delivery words/trip, trips 8\\)" "rvtt_launch_flatten" } }
// { dg-final { scan-tree-dump-not "launch-flatten-unpriced-builtin" "rvtt_launch_flatten" } }

extern volatile unsigned long lf_instrn_buffer[];

void lf_computed_word_marked ()
{
  unsigned dst_offset = 0;
  for (unsigned d = 0; d < 8; ++d)
    {
      lf_instrn_buffer[0] = 0x70000000u + (dst_offset << 0);
      __builtin_rvtt_sfprawlreg_effect (0x00, 0x01);
      lf_instrn_buffer[0] = 0x70000000u + ((dst_offset + 4) << 0);
      __builtin_rvtt_sfprawlreg_effect (0x00, 0x02);
      auto a = __builtin_rvtt_sfpreadlreg (0);
      auto b = __builtin_rvtt_sfpreadlreg (1);
      auto r = __builtin_rvtt_sfpswap_indexed (a, b, a, b, 1);
      __builtin_rvtt_sfpwritelreg (__builtin_rvtt_sfpselect4 (r, 0), 0);
      __builtin_rvtt_sfpwritelreg (__builtin_rvtt_sfpselect4 (r, 1), 1);
      lf_instrn_buffer[0] = 0x72000000u + (dst_offset << 0);
      __builtin_rvtt_sfprawlreg_effect (0x01, 0x00);
      lf_instrn_buffer[0] = 0x72000000u + ((dst_offset + 4) << 0);
      __builtin_rvtt_sfprawlreg_effect (0x02, 0x00);
      dst_offset += 8;
    }
}
