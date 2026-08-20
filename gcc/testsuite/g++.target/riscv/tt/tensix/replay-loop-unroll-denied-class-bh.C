// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-replay-loop-unroll -fdump-tree-rvtt_replay_unroll" }
// A SETC16 in the row programs machine state outside the row: its
// repetition semantics are not the row's.  Refuse by name.
// { dg-final { scan-tree-dump "refused .replay-loop-unroll-denied-class." "rvtt_replay_unroll" } }
// { dg-final { scan-assembler-not "TTREPLAY" } }

void rlu_denied_class ()
{
  for (int d = 0; d < 32; ++d)
    {
      auto v = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      auto a = __builtin_rvtt_sfpabs (v, 1);
      auto c = __builtin_rvtt_sfploadi (nullptr, 0x3f00, 0, 0, 0);
      auto t = __builtin_rvtt_sfpmad (a, c, v, 0);
      __builtin_rvtt_ttsetc16 (18, 0);
      __builtin_rvtt_sfpstore (nullptr, t, 0, 0, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
