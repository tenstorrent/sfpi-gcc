// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-replay-loop-unroll -fdump-tree-rvtt_replay_unroll" }
// A two-word row cannot form a replay window (former MIN_SEQUENCE);
// unrolling it would be pure code growth.  Refuse by name.
// { dg-final { scan-tree-dump "refused .replay-loop-unroll-row-too-small." "rvtt_replay_unroll" } }

void rlu_row_too_small ()
{
  for (int d = 0; d < 32; ++d)
    {
      auto v = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      __builtin_rvtt_sfpstore (nullptr, v, 0, 0, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
