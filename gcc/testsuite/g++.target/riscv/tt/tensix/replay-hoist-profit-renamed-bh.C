// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-hoist -fdump-rtl-rvtt_replay-details" }
// { dg-final { scan-rtl-dump "Hoist profitable: modeled benefit 152 >= 64" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-times "Counted-loop replay payload" 1 "rvtt_replay" } }
// { dg-final { scan-rtl-dump "Not hoisting: modeled benefit 19 < 64" "rvtt_replay" } }

// Renamed-equivalent, constant-varied copies of the fire and refuse shapes:
// different function names, different opcodes (add instead of mul), and a
// different source LREG must not change the decision, which depends only on
// the provable trip count, the capture length, and the cost table.

void totally_different_name_same_shape ()
{
  auto y = __builtin_rvtt_sfpreadlreg (1);
  for (unsigned rep = 0; rep != 16; ++rep)
    {
      y = __builtin_rvtt_sfpadd (y, y, 0);
      y = __builtin_rvtt_sfpadd (y, y, 0);
      y = __builtin_rvtt_sfpadd (y, y, 0);
      y = __builtin_rvtt_sfpadd (y, y, 0);
      y = __builtin_rvtt_sfpadd (y, y, 0);
      y = __builtin_rvtt_sfpadd (y, y, 0);
      y = __builtin_rvtt_sfpadd (y, y, 0);
      y = __builtin_rvtt_sfpadd (y, y, 0);
      y = __builtin_rvtt_sfpadd (y, y, 0);
      y = __builtin_rvtt_sfpadd (y, y, 0);
    }
  __builtin_rvtt_sfpwritelreg (y, 1);
}

void another_name_for_the_losing_shape ()
{
  auto z = __builtin_rvtt_sfpreadlreg (2);
  for (unsigned k = 0; k != 3; ++k)
    {
      z = __builtin_rvtt_sfpadd (z, z, 0);
      z = __builtin_rvtt_sfpadd (z, z, 0);
      z = __builtin_rvtt_sfpadd (z, z, 0);
      z = __builtin_rvtt_sfpadd (z, z, 0);
      z = __builtin_rvtt_sfpadd (z, z, 0);
      z = __builtin_rvtt_sfpadd (z, z, 0);
      z = __builtin_rvtt_sfpadd (z, z, 0);
      z = __builtin_rvtt_sfpadd (z, z, 0);
      z = __builtin_rvtt_sfpadd (z, z, 0);
      z = __builtin_rvtt_sfpadd (z, z, 0);
      z = __builtin_rvtt_sfpadd (z, z, 0);
      z = __builtin_rvtt_sfpadd (z, z, 0);
      z = __builtin_rvtt_sfpadd (z, z, 0);
      z = __builtin_rvtt_sfpadd (z, z, 0);
      z = __builtin_rvtt_sfpadd (z, z, 0);
      z = __builtin_rvtt_sfpadd (z, z, 0);
      z = __builtin_rvtt_sfpadd (z, z, 0);
      z = __builtin_rvtt_sfpadd (z, z, 0);
      z = __builtin_rvtt_sfpadd (z, z, 0);
      z = __builtin_rvtt_sfpadd (z, z, 0);
    }
  __builtin_rvtt_sfpwritelreg (z, 2);
}
