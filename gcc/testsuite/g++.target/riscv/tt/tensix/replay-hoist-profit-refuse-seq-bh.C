// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-hoist -fdump-rtl-rvtt_replay-details" }
// { dg-final { scan-rtl-dump "Not hoisting: modeled benefit -696 < 60" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "Hoisted no-exec capture" "rvtt_replay" } }
// { dg-final { scan-rtl-dump "Capturing and executing sequence" "rvtt_replay" } }

// Repeated-sequence analog of the hardware-regressing scout shape (the
// Log/Log1p class: a DELIVERY-bound payload at a low trip count): a
// three-trip loop whose body re-records a repeated eight-word sequence
// of four rotating accumulators every trip (zero modeled interlock
// stalls; one op varied between rounds so no shorter period exists: exec = 800 < deliver_record 1107).  The hoist must refuse
// (3 * (1107 - 870) - 1407 = -696, far below the cost-table minimum of
// 60) and the capture must stay in the loop as an ordinary
// record-with-execute, exactly as with the hoist disabled.
void seq_refuse_3trip ()
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  auto y = __builtin_rvtt_sfpreadlreg (1);
  auto z = __builtin_rvtt_sfpreadlreg (2);
  auto w = __builtin_rvtt_sfpreadlreg (3);
  for (unsigned ix = 0; ix != 3; ++ix)
    {
      x = __builtin_rvtt_sfpmul (x, x, 0);
      y = __builtin_rvtt_sfpmul (y, y, 0);
      z = __builtin_rvtt_sfpmul (z, z, 0);
      w = __builtin_rvtt_sfpmul (w, w, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      y = __builtin_rvtt_sfpmul (y, y, 0);
      z = __builtin_rvtt_sfpadd (z, z, 0);
      w = __builtin_rvtt_sfpmul (w, w, 0);
      __builtin_rvtt_ttincrwc (0, 4, 0, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      y = __builtin_rvtt_sfpmul (y, y, 0);
      z = __builtin_rvtt_sfpmul (z, z, 0);
      w = __builtin_rvtt_sfpmul (w, w, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      y = __builtin_rvtt_sfpmul (y, y, 0);
      z = __builtin_rvtt_sfpadd (z, z, 0);
      w = __builtin_rvtt_sfpmul (w, w, 0);
      __builtin_rvtt_ttincrwc (0, 4, 0, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
  __builtin_rvtt_sfpwritelreg (y, 1);
  __builtin_rvtt_sfpwritelreg (z, 2);
  __builtin_rvtt_sfpwritelreg (w, 3);
}
