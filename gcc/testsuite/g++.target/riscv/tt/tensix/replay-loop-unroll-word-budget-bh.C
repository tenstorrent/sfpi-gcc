// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-replay-loop-unroll -fdump-tree-rvtt_replay_unroll" }
// A row too large for the unrolled-group word budget refuses by name.
// { dg-final { scan-tree-dump "refused .replay-loop-unroll-word-budget." "rvtt_replay_unroll" } }

void rlu_word_budget ()
{
  for (int d = 0; d < 32; ++d)
    {
      auto t0 = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      auto c = __builtin_rvtt_sfploadi (nullptr, 0x3f00, 0, 0, 0);
      auto t1 = __builtin_rvtt_sfpmad (t0, c, t0, 0);
      auto t2 = __builtin_rvtt_sfpmad (t1, c, t1, 0);
      auto t3 = __builtin_rvtt_sfpmad (t2, c, t2, 0);
      auto t4 = __builtin_rvtt_sfpmad (t3, c, t3, 0);
      auto t5 = __builtin_rvtt_sfpmad (t4, c, t4, 0);
      auto t6 = __builtin_rvtt_sfpmad (t5, c, t5, 0);
      auto t7 = __builtin_rvtt_sfpmad (t6, c, t6, 0);
      auto t8 = __builtin_rvtt_sfpmad (t7, c, t7, 0);
      auto t9 = __builtin_rvtt_sfpmad (t8, c, t8, 0);
      auto t10 = __builtin_rvtt_sfpmad (t9, c, t9, 0);
      auto t11 = __builtin_rvtt_sfpmad (t10, c, t10, 0);
      auto t12 = __builtin_rvtt_sfpmad (t11, c, t11, 0);
      auto t13 = __builtin_rvtt_sfpmad (t12, c, t12, 0);
      auto t14 = __builtin_rvtt_sfpmad (t13, c, t13, 0);
      auto t15 = __builtin_rvtt_sfpmad (t14, c, t14, 0);
      auto t16 = __builtin_rvtt_sfpmad (t15, c, t15, 0);
      auto t17 = __builtin_rvtt_sfpmad (t16, c, t16, 0);
      auto t18 = __builtin_rvtt_sfpmad (t17, c, t17, 0);
      auto t19 = __builtin_rvtt_sfpmad (t18, c, t18, 0);
      auto t20 = __builtin_rvtt_sfpmad (t19, c, t19, 0);
      auto t21 = __builtin_rvtt_sfpmad (t20, c, t20, 0);
      auto t22 = __builtin_rvtt_sfpmad (t21, c, t21, 0);
      auto t23 = __builtin_rvtt_sfpmad (t22, c, t22, 0);
      auto t24 = __builtin_rvtt_sfpmad (t23, c, t23, 0);
      auto t25 = __builtin_rvtt_sfpmad (t24, c, t24, 0);
      auto t26 = __builtin_rvtt_sfpmad (t25, c, t25, 0);
      auto t27 = __builtin_rvtt_sfpmad (t26, c, t26, 0);
      auto t28 = __builtin_rvtt_sfpmad (t27, c, t27, 0);
      auto t29 = __builtin_rvtt_sfpmad (t28, c, t28, 0);
      auto t30 = __builtin_rvtt_sfpmad (t29, c, t29, 0);
      auto t31 = __builtin_rvtt_sfpmad (t30, c, t30, 0);
      auto t32 = __builtin_rvtt_sfpmad (t31, c, t31, 0);
      auto t33 = __builtin_rvtt_sfpmad (t32, c, t32, 0);
      auto t34 = __builtin_rvtt_sfpmad (t33, c, t33, 0);
      __builtin_rvtt_sfpstore (nullptr, t34, 0, 0, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
