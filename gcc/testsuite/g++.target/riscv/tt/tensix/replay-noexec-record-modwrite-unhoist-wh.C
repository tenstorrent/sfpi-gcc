// The WH mirror of
// replay-noexec-record-modwrite-unhoist-bh.C -- the Wormhole
// capability entry carries the same audited W_drain window
// (same-frontend-class conservative adoption, rvtt-cost.md), so the
// placement obligation refuses identically.
// { dg-options "-mcpu=tt-wh-tensix -fno-exceptions -fno-rtti -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-hoist -mtt-tensix-replay-hoist-min-benefit=0 -fdump-rtl-rvtt_replay" }
// { dg-final { scan-rtl-dump "Replay refusal: noexec-record-modwrite-window-unaudited" "rvtt_replay" } }
// { dg-final { scan-assembler-not "TTREPLAY" } }
// { dg-final { scan-assembler "TTSETC16\t2, 8224" } }

void nlk_seeded_walk_wh ()
{
  auto a = __builtin_rvtt_sfpreadlreg (0);
  auto b = __builtin_rvtt_sfpreadlreg (1);
  auto c = __builtin_rvtt_sfpreadlreg (2);
  auto d = __builtin_rvtt_sfpreadlreg (3);
  auto e = __builtin_rvtt_sfpreadlreg (4);
  auto f = __builtin_rvtt_sfpreadlreg (5);
  auto g = __builtin_rvtt_sfpreadlreg (6);
  auto h = __builtin_rvtt_sfpreadlreg (7);
  __builtin_rvtt_ttsetc16 (2, 0x2020);
  for (unsigned kk = 0; kk != 16; ++kk)
    {
      a = __builtin_rvtt_sfpmul (a, e, 0);
      b = __builtin_rvtt_sfpmul (b, f, 0);
      c = __builtin_rvtt_sfpmul (c, g, 0);
      d = __builtin_rvtt_sfpmul (d, h, 0);
      a = __builtin_rvtt_sfpmul (a, f, 0);
      b = __builtin_rvtt_sfpmul (b, g, 0);
      c = __builtin_rvtt_sfpmul (c, h, 0);
      d = __builtin_rvtt_sfpmul (d, e, 0);
      a = __builtin_rvtt_sfpmul (a, g, 0);
      b = __builtin_rvtt_sfpmul (b, h, 0);
      c = __builtin_rvtt_sfpmul (c, e, 0);
      d = __builtin_rvtt_sfpmul (d, f, 0);
    }
  __builtin_rvtt_sfpwritelreg (a, 0);
  __builtin_rvtt_sfpwritelreg (b, 1);
  __builtin_rvtt_sfpwritelreg (c, 2);
  __builtin_rvtt_sfpwritelreg (d, 3);
}
