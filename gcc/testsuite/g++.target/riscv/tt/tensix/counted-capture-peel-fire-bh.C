// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-hoist -mtt-tensix-optimize-counted-capture-peel -fdump-rtl-rvtt_replay-details" }
// Check that a counted row loop too unprofitable to hoist is peeled into an exec-while-record capture plus playbacks.
// { dg-final { scan-rtl-dump "Not hoisting: modeled benefit -495 < 60" "rvtt_replay" } }
// { dg-final { scan-rtl-dump "Counted-peel pricing \\(loop 1\\): trips 8, words 8, exec_ilk 8 slots, before 984, after 870, peel_cost 423, benefit 375" "rvtt_replay" } }
// { dg-final { scan-rtl-dump "counted-capture-peel admitted: counted-loop bb 3 peeled exec-while-record \\(trips 8 -> 7\\)" "rvtt_replay" } }
// { dg-final { scan-assembler-times "TTREPLAY\t0, 8, 1, 1" 1 } }
// { dg-final { scan-assembler-times "TTREPLAY\t0, 8, 0, 0" 7 } }
static volatile int sink;
void counted_peel_fire ()
{
  auto a = __builtin_rvtt_sfpreadlreg (0);
  auto b = __builtin_rvtt_sfpreadlreg (1);
  auto c = __builtin_rvtt_sfpreadlreg (2);
  for (unsigned i = 0; i != 8; ++i)
    {
      auto t0 = __builtin_rvtt_sfpmul (a, b, 0);
      auto t1 = __builtin_rvtt_sfpmul (b, c, 0);
      auto t2 = __builtin_rvtt_sfpmul (c, a, 0);
      auto t3 = __builtin_rvtt_sfpmul (a, c, 0);
      auto t4 = __builtin_rvtt_sfpmul (b, a, 0);
      a = __builtin_rvtt_sfpmul (t0, t1, 0);
      b = __builtin_rvtt_sfpmul (t2, t3, 0);
      c = __builtin_rvtt_sfpmul (t4, t4, 0);
    }
  __builtin_rvtt_sfpwritelreg (a, 0);
  __builtin_rvtt_sfpwritelreg (b, 1);
  __builtin_rvtt_sfpwritelreg (c, 2);
}
