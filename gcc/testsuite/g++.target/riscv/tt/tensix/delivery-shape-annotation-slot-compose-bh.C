// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-delivery-shape -mtt-tensix-optimize-replay-loop-unroll -mtt-tensix-optimize-round-interleave -fdump-tree-rvtt_delivery_shape -fdump-tree-rvtt_replay_unroll -fdump-tree-rvtt_round_interleave" }
// Composition twin (FH audit, INV PR8; the pair census had ZERO twins for
// these pairs): the annotation-slot precedence chain over one TU carrying
// both a window-class row loop and a round loop.  The solver owns BOTH
// decision slots -- an affirmative ROLLED selection on the row loop and a
// requested unroll on the round loop -- and neither downstream annotation
// pass overrides either decision.
// { dg-final { scan-tree-dump "delivery-shape: selected rolled" "rvtt_delivery_shape" } }
// { dg-final { scan-tree-dump "delivery-shape: requested unroll 2 of loop" "rvtt_delivery_shape" } }
// { dg-final { scan-tree-dump-times "replay-loop-unroll: fires=0 refusals=0" 2 "rvtt_replay_unroll" } }
// { dg-final { scan-tree-dump-times "round-interleave: fires=0" 2 "rvtt_round_interleave" } }
extern volatile unsigned int __instrn_buffer[];
__attribute__((noinline)) void
fh_comp_rows (int tiles)
{
  for (int t = 0; t != tiles; ++t)
    for (int row = 0; row != 32; ++row)
      {
	auto c = __builtin_rvtt_sfpxloadi (nullptr, 0x3ecccccd, 0, 0, -32);
	auto x = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);
	x = __builtin_rvtt_sfpmad (x, c, c, 0);
	__builtin_rvtt_sfpstore (nullptr, x, 0, 0, 0, 6, 7);
	__builtin_rvtt_ttincrwc (0, 2, 0, 0);
      }
}
__attribute__((noinline)) void
fh_comp_rounds ()
{
  auto x   = __builtin_rvtt_sfpreadlreg (0);
  auto acc = __builtin_rvtt_sfpreadlreg (2);
  for (unsigned r = 0; r != 32; ++r)
    {
      auto t1 = __builtin_rvtt_sfpmul (x, x, 0);
      auto t2 = __builtin_rvtt_sfpmad (t1, x, x, 0);
      acc = __builtin_rvtt_sfpand (acc, t2);
    }
  __builtin_rvtt_sfpwritelreg (acc, 2);
}
