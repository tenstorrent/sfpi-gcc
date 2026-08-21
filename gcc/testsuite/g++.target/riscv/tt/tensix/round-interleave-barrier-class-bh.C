// Scheduler-barrier word classes refuse at the REQUEST stage: a Dst
// row loop (loads/stores/counter steps) is replay-loop-unroll's
// territory, not the interleave's -- its words are dst-access/rwc-step
// barriers the post-RA scheduler will never move, so requesting the
// doubled body would be pure code growth.  The gimple census refuses
// round-interleave-body-barrier-class, and the RTL cyclic extension
// independently keeps the still-rolled row deferred (the row's Tensix
// barrier words break the modeled seam).  A CC-writing round body
// refuses the same way.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap -mtt-tensix-optimize-round-interleave -mno-tt-tensix-optimize-replay -fdump-tree-rvtt_round_interleave -fdump-rtl-rvtt_schedule-details" }
// { dg-final { scan-tree-dump-times "refused \\(round-interleave-body-barrier-class\\)" 2 "rvtt_round_interleave" } }
// { dg-final { scan-tree-dump-not "requested unroll" "rvtt_round_interleave" } }
// { dg-final { scan-rtl-dump "List-schedule deferred: cyclic row adjacency in bb \\d+ \\(round-interleave-seam-barrier-word\\)" "rvtt_schedule" } }
// { dg-final { scan-rtl-dump-not "round-interleave cyclic" "rvtt_schedule" } }

void dst_row_loop ()
{
  for (int d = 0; d < 32; ++d)
    {
      auto v = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      auto a = __builtin_rvtt_sfpabs (v, 1);
      auto t = __builtin_rvtt_sfpmad (a, v, v, 0);
      __builtin_rvtt_sfpstore (nullptr, t, 0, 0, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}

void cc_round_loop ()
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  auto s = __builtin_rvtt_sfpreadlreg (2);
  for (unsigned r = 0; r != 32; ++r)
    {
      auto t1 = __builtin_rvtt_sfpmul (x, x, 0);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc_v (t1, 0);
      __builtin_rvtt_sfppopc (0);
      s = __builtin_rvtt_sfpxor (s, t1);
    }
  __builtin_rvtt_sfpwritelreg (s, 2);
}
