// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-latency-schedule -mtt-tensix-optimize-interlock-schedule -mtt-tensix-optimize-counted-row-formation -mtt-tensix-optimize-replay-loop-unroll -fdump-rtl-rvtt_replay" }
// Preservation twin of counted-row-vacated-shadow-refuse-bh.C: the same
// row with the min/max SFPSWAP replaced by a MAD-family consumer -- a
// read the BH scoreboard DOES detect (not on the SFPMAD.md CAUTION
// list) -- so the vacated slot needs no software pad and the family
// still canonicalizes and forms.  Proves the refusal keys on the
// erratum consumer, not on the member moves themselves.
// { dg-final { scan-rtl-dump-not "counted-row-vacated-delay-shadow" "rvtt_replay" } }
// { dg-final { scan-rtl-dump "Canonicalized counted-row family" "rvtt_replay" } }
// { dg-final { scan-rtl-dump "Formed counted-row record" "rvtt_replay" } }

void covered_consumer_kernel ()
{
  for (int d = 0; d < 32; ++d)
    {
      auto v  = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
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
      auto pair = __builtin_rvtt_sfpmad (m, one, t3, 0);
      auto lo = pair;
      __builtin_rvtt_sfpstore (nullptr, lo, 0, 0, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
