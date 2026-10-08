// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti --param=max-completely-peeled-insns=1 -mtt-tensix-optimize-launch-flatten -fdump-tree-rvtt_launch_flatten -fdump-tree-cunroll-details" }
// Explicit lifetimes around raw delivery are over-priced by the generic
// unroller even when no register moves are needed.  Keep early generic
// unrolling disabled by its size limit; the bounded target request must fire.
// { dg-final { scan-tree-dump "launch-flatten: requested complete unroll" "rvtt_launch_flatten" } }
// { dg-final { scan-tree-dump "completely unrolled" "cunroll" } }

extern volatile unsigned lf_lreg_buffer[];

void lf_lreg_delivery ()
{
  for (unsigned d = 0; d < 8; ++d)
    {
      lf_lreg_buffer[0] = 0x70000000u + d * 4;
      auto a = __builtin_rvtt_sfpreadlreg (0);
      lf_lreg_buffer[0] = 0x70100000u + d * 4;
      auto b = __builtin_rvtt_sfpreadlreg (1);
      __builtin_rvtt_sfpwritelreg (a, 0);
      __builtin_rvtt_sfpwritelreg (b, 1);
      lf_lreg_buffer[0] = 0x72000000u + d * 4;
      lf_lreg_buffer[0] = 0x72100000u + d * 4;
    }
}
