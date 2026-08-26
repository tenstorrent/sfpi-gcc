// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-launch-flatten -fdump-tree-rvtt_launch_flatten" }
// Computed-word delivery (the LLK TT_ macro shape, the topk merge/rebuild
// loop class): per trip the body pushes runtime-computed instruction
// words through the volatile instruction buffer around a typed swap.
// The flatten request lets the complete unroller fold every word to a
// constant -- exactly what the raw-word arm's unroll has always done.
// { dg-final { scan-tree-dump "launch-flatten: requested complete unroll of loop \[0-9\]+ \\(~\[0-9\]+ delivery words/trip, trips 8\\)" "rvtt_launch_flatten" } }

extern volatile unsigned lf_instrn_buffer[];

void lf_computed_word ()
{
  unsigned dst_offset = 0;
  for (unsigned d = 0; d < 8; ++d)
    {
      lf_instrn_buffer[0] = 0x70000000u + (dst_offset << 0);
      lf_instrn_buffer[0] = 0x70000000u + ((dst_offset + 4) << 0);
      auto a = __builtin_rvtt_sfpreadlreg (0);
      auto b = __builtin_rvtt_sfpreadlreg (1);
      auto r = __builtin_rvtt_sfpswap_indexed (a, b, a, b, 1);
      __builtin_rvtt_sfpwritelreg (__builtin_rvtt_sfpselect4 (r, 0), 0);
      __builtin_rvtt_sfpwritelreg (__builtin_rvtt_sfpselect4 (r, 1), 1);
      lf_instrn_buffer[0] = 0x72000000u + (dst_offset << 0);
      lf_instrn_buffer[0] = 0x72000000u + ((dst_offset + 4) << 0);
      dst_offset += 8;
    }
}
