// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-launch-flatten -fdump-tree-rvtt_launch_flatten" }
// A non-.ttinsn asm in the body is not a fixed delivery word: refuse
// by name.
// { dg-final { scan-tree-dump "refused .launch-flatten-foreign-asm." "rvtt_launch_flatten" } }

void lf_foreign_asm ()
{
  for (int d = 0; d < 8; ++d)
    {
      __builtin_rvtt_ttreplay (nullptr, 9, 0, 0, 16, 0, 0);
      __builtin_rvtt_ttreplay (nullptr, 9, 0, 0, 16, 0, 0);
      __builtin_rvtt_ttreplay (nullptr, 9, 0, 0, 16, 0, 0);
      __builtin_rvtt_ttreplay (nullptr, 9, 0, 0, 16, 0, 0);
      asm volatile ("nop");
    }
}
