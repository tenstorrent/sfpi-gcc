// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-latency-schedule -mtt-tensix-optimize-interlock-schedule -mtt-tensix-optimize-counted-row-formation -mtt-tensix-optimize-replay-loop-unroll -fdump-rtl-rvtt_replay" }
// WH sibling of counted-row-vacated-shadow-refuse-bh.C: Wormhole has no
// result scoreboard at all, so ANY dependent consumer put directly behind
// a DYNAMIC-delay producer by the canonicalization's member moves is an
// undischarged shadow -- the erratum-mask path of the check degenerates
// to plain dependence, and the family refuses by the same name.
// { dg-final { scan-rtl-dump "counted-row-vacated-delay-shadow" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "Canonicalized counted-row family" "rvtt_replay" } }
// { dg-final { scan-assembler "TTREPLAY\t\[0-9\]+, \[0-9\]+, 1, 1" } }

void vacated_shadow_kernel_wh ()
{
  for (int d = 0; d < 32; ++d)
    {
      auto v  = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 3);
      auto a  = __builtin_rvtt_sfpabs (v, 1);
      auto r0 = __builtin_rvtt_sfploadi (nullptr, 0x66db, 0, 0, 0);
      auto t1 = __builtin_rvtt_sfpmad (a, r0, v, 0);
      auto t2 = __builtin_rvtt_sfpmad (t1, a, r0, 0);
      auto t3 = __builtin_rvtt_sfpmad (t2, a, t1, 0);
      auto t4 = __builtin_rvtt_sfpmad (t3, a, t2, 0);
      auto r1 = __builtin_rvtt_sfploadi (nullptr, 0x3f7f, 0, 0, 0);
      auto t5 = __builtin_rvtt_sfpmad (t4, a, r1, 0);
      auto m  = __builtin_rvtt_sfpmul (t5, a, 0);
      auto one = __builtin_rvtt_sfpreadlreg (10);
      auto pair = __builtin_rvtt_sfpswap (m, one, 1);
      auto lo = __builtin_rvtt_sfpselect2 (pair, 0);
      __builtin_rvtt_sfpstore (nullptr, lo, 0, 0, 0, 0, 3);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
