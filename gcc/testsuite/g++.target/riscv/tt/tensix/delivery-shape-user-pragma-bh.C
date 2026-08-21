// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-delivery-shape -fdump-tree-rvtt_delivery_shape" }
// A user's own unroll annotation is never overridden: the solver makes
// no decision at all, and the user's factor shapes the capture.
// { dg-final { scan-tree-dump-not "requested unroll" "rvtt_delivery_shape" } }
// { dg-final { scan-tree-dump-not "selected rolled" "rvtt_delivery_shape" } }
// { dg-final { scan-assembler "TTREPLAY\t0, \[0-9\]+, 1, 1" } }

void ds_user_pragma ()
{
#pragma GCC unroll 4
  for (int d = 0; d < 32; ++d)
    {
      auto v = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      auto a = __builtin_rvtt_sfpabs (v, 1);
      auto c = __builtin_rvtt_sfploadi (nullptr, 0x3f00, 0, 0, 0);
      auto t = __builtin_rvtt_sfpmad (a, c, v, 0);
      __builtin_rvtt_sfpstore (nullptr, t, 0, 0, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
