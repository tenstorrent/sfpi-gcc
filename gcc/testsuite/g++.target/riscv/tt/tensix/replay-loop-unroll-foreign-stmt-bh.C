// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-replay-loop-unroll -fdump-tree-rvtt_replay_unroll" }
// A non-rvtt call in the row body refuses by name.
// { dg-final { scan-tree-dump "refused .replay-loop-unroll-foreign-stmt." "rvtt_replay_unroll" } }
// { dg-final { scan-assembler-not "TTREPLAY" } }

extern void rlu_external_hook ();

void rlu_foreign_stmt ()
{
  for (int d = 0; d < 32; ++d)
    {
      auto v = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      auto a = __builtin_rvtt_sfpabs (v, 1);
      auto c = __builtin_rvtt_sfploadi (nullptr, 0x3f00, 0, 0, 0);
      auto t = __builtin_rvtt_sfpmad (a, c, v, 0);
      rlu_external_hook ();
      __builtin_rvtt_sfpstore (nullptr, t, 0, 0, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
