// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-hoist -fdump-rtl-rvtt_replay-details" }
// { dg-final { scan-rtl-dump "Hoist pricing .loop \\d+.: trips 5, words 6, exec_ilk 11 slots .re-record body, execution-bound., deliver_body 738, deliver_record 861, record 300, before 1400, after 1170, benefit 850 .min 60." "rvtt_replay" } }
// { dg-final { scan-rtl-dump "Hoist profitable: modeled benefit 850 >= 60" "rvtt_replay" } }
// { dg-final { scan-rtl-dump "Hoisted no-exec capture" "rvtt_replay" } }

// Check that an execution-bound re-record hoist fires with different operation, length, trip count and names.
void renamed_execbound_variant ()
{
  auto v = __builtin_rvtt_sfpreadlreg (6);
  for (unsigned trip = 0; trip != 5; ++trip)
    {
      v = __builtin_rvtt_sfpadd (v, v, 0);
      v = __builtin_rvtt_sfpadd (v, v, 0);
      v = __builtin_rvtt_sfpadd (v, v, 0);
      v = __builtin_rvtt_sfpadd (v, v, 0);
      v = __builtin_rvtt_sfpadd (v, v, 0);
      v = __builtin_rvtt_sfpmul (v, v, 0);
      __builtin_rvtt_ttincrwc (0, 4, 0, 0);
      v = __builtin_rvtt_sfpadd (v, v, 0);
      v = __builtin_rvtt_sfpadd (v, v, 0);
      v = __builtin_rvtt_sfpadd (v, v, 0);
      v = __builtin_rvtt_sfpadd (v, v, 0);
      v = __builtin_rvtt_sfpadd (v, v, 0);
      v = __builtin_rvtt_sfpmul (v, v, 0);
      __builtin_rvtt_ttincrwc (0, 4, 0, 0);
    }
  __builtin_rvtt_sfpwritelreg (v, 6);
}
