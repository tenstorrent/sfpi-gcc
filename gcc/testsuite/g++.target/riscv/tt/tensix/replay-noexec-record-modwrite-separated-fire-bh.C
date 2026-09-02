// No-exec-record placement rule, fire direction: the SAME counted-loop hoist shape
// with the user mod-write separated from the record placement by seven
// audited issue-time words (>= W_drain=7 on every path) is the PROVEN
// class -- the sweep leaves the hoist untouched and the no-exec
// capture plus playback survive.  Distance, not presence, is the
// witnessed boundary (rvtt-cost.md).
// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-hoist -mtt-tensix-replay-hoist-min-benefit=0 -fdump-rtl-rvtt_replay" }
// { dg-final { scan-rtl-dump "Hoisted no-exec capture" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "noexec-record-modwrite-window-unaudited" "rvtt_replay" } }
// { dg-final { scan-assembler "TTREPLAY" } }
// { dg-final { scan-assembler "TTSETC16\t2, 8224" } }

void nlk_seeded_walk_separated ()
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
  __builtin_rvtt_ttincrwc (0, 1, 0, 0);
  __builtin_rvtt_ttincrwc (0, 1, 0, 0);
  __builtin_rvtt_ttincrwc (0, 1, 0, 0);
  __builtin_rvtt_ttincrwc (0, 1, 0, 0);
  __builtin_rvtt_ttincrwc (0, 1, 0, 0);
  __builtin_rvtt_ttincrwc (0, 1, 0, 0);
  __builtin_rvtt_ttincrwc (0, 1, 0, 0);
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
