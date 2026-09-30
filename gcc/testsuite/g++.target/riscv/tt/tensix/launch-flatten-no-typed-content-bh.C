// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-launch-flatten -fdump-tree-rvtt_launch_flatten" }
// A body with NO typed SFPU word (launch owners and raw words only) IS
// the raw-spelling world: its size pricing is already word-accurate,
// and the request must not grant raw code an unroll that pricing
// correctly refused (the topk_xl overflow/regression class).  Refuse
// by name; bytes stay rolled.
// { dg-final { scan-tree-dump "refused .launch-flatten-no-typed-content." "rvtt_launch_flatten" } }
// { dg-final { scan-assembler-times "TTREPLAY\t16, 9, 0, 0" 4 } }

void lf_raw_only ()
{
  for (int d = 0; d < 8; ++d)
    {
      __builtin_rvtt_ttreplay (nullptr, 9, 0, 0, 16, 0, 0);
      __builtin_rvtt_ttreplay (nullptr, 9, 0, 0, 16, 0, 0);
      __builtin_rvtt_ttreplay (nullptr, 9, 0, 0, 16, 0, 0);
      __builtin_rvtt_ttreplay (nullptr, 9, 0, 0, 16, 0, 0);
      asm volatile (".ttinsn %0" :: "n" (0x91800104u));
    }
}
