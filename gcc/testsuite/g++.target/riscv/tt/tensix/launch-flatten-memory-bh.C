// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-launch-flatten -fdump-tree-rvtt_launch_flatten" }
// A PLAIN (non-volatile) memory store in the body: frozen scalar state,
// not delivery -- refuse by name.  (The volatile spelling of the same
// store is the TT_ computed-word delivery class and ADMITS: see the
// computed-word fire twin.)
// { dg-final { scan-tree-dump "refused .launch-flatten-memory." "rvtt_launch_flatten" } }

int lf_side_channel;

void lf_memory ()
{
  for (int d = 0; d < 8; ++d)
    {
      __builtin_rvtt_ttreplay (nullptr, 9, 0, 0, 16, 0, 0);
      __builtin_rvtt_ttreplay (nullptr, 9, 0, 0, 16, 0, 0);
      __builtin_rvtt_ttreplay (nullptr, 9, 0, 0, 16, 0, 0);
      __builtin_rvtt_ttreplay (nullptr, 9, 0, 0, 16, 0, 0);
      lf_side_channel = d;
    }
}
