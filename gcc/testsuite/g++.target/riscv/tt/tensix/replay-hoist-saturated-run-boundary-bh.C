// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-hoist -fdump-rtl-rvtt_replay-details" }
// { dg-final { scan-rtl-dump "Hoist profitable: modeled benefit 100 >= 60" "rvtt_replay" } }
// { dg-final { scan-rtl-dump "Record delivery hidden: contiguous launch run 3 exec surplus 831 >= record delivery 615" "rvtt_replay" } }
// { dg-final { scan-rtl-dump "Not hoisting: modeled benefit -915 < 60" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-times "Hoisted no-exec capture" 1 "rvtt_replay" } }

// Structural saturation boundary (delivery-bound payloads), no flag
// interplay: contiguity here is literal (no typed increments at all;
// sibling groups are separated by ordinary delivered payload words with
// per-site unique immediates, and the sibling payload is
// phase-asymmetric -- mul,mul,add,mul over four rotating accumulators,
// zero modeled interlock stalls, exec = 400 < deliver_record 615 -- so
// the sequence former cannot re-segment contiguous copies into a longer
// period).
//
// run_two: four sibling copies of the four-slot payload in contiguous
// PAIRS, seven trips.  A run of 2 has execution surplus
// 2 * (400 - 123) = 554 < 615, one delivered word short of hiding the
// record pass: still delivery-bound arithmetic, fires at
// 7 * (615 - 470) - 915 = 100 >= 60.  This is the
// just-under-saturation boundary fire.
void run_two ()
{
  auto a = __builtin_rvtt_sfpreadlreg (0);
  auto b = __builtin_rvtt_sfpreadlreg (1);
  auto c = __builtin_rvtt_sfpreadlreg (2);
  auto d = __builtin_rvtt_sfpreadlreg (3);
  for (unsigned ix = 0; ix != 7; ++ix)
    {
      a = __builtin_rvtt_sfpmul (a, a, 0);
      b = __builtin_rvtt_sfpmul (b, b, 0);
      c = __builtin_rvtt_sfpadd (c, c, 0);
      d = __builtin_rvtt_sfpmul (d, d, 0);
      a = __builtin_rvtt_sfpmul (a, a, 0);
      b = __builtin_rvtt_sfpmul (b, b, 0);
      c = __builtin_rvtt_sfpadd (c, c, 0);
      d = __builtin_rvtt_sfpmul (d, d, 0);
      a = __builtin_rvtt_sfpmuli (nullptr, a, 0x3a12, 0, 0, 0);
      a = __builtin_rvtt_sfpmul (a, a, 0);
      b = __builtin_rvtt_sfpmul (b, b, 0);
      c = __builtin_rvtt_sfpadd (c, c, 0);
      d = __builtin_rvtt_sfpmul (d, d, 0);
      a = __builtin_rvtt_sfpmul (a, a, 0);
      b = __builtin_rvtt_sfpmul (b, b, 0);
      c = __builtin_rvtt_sfpadd (c, c, 0);
      d = __builtin_rvtt_sfpmul (d, d, 0);
      a = __builtin_rvtt_sfpmuli (nullptr, a, 0x3b47, 0, 0, 0);
    }
  __builtin_rvtt_sfpwritelreg (a, 0);
  __builtin_rvtt_sfpwritelreg (b, 1);
  __builtin_rvtt_sfpwritelreg (c, 2);
  __builtin_rvtt_sfpwritelreg (d, 3);
}

// run_three: six sibling copies in contiguous TRIPLES, seven trips.  A
// run of 3 has execution surplus 3 * (400 - 123) = 831 >= 615: the
// record delivery hides under the run's execution, the modeled benefit
// degenerates to 7 * 0 - 915 = -915, and the capture stays in the loop
// as an ordinary record-with-execution.
void run_three ()
{
  auto a = __builtin_rvtt_sfpreadlreg (0);
  auto b = __builtin_rvtt_sfpreadlreg (1);
  auto c = __builtin_rvtt_sfpreadlreg (2);
  auto d = __builtin_rvtt_sfpreadlreg (3);
  for (unsigned ix = 0; ix != 7; ++ix)
    {
      a = __builtin_rvtt_sfpmul (a, a, 0);
      b = __builtin_rvtt_sfpmul (b, b, 0);
      c = __builtin_rvtt_sfpadd (c, c, 0);
      d = __builtin_rvtt_sfpmul (d, d, 0);
      a = __builtin_rvtt_sfpmul (a, a, 0);
      b = __builtin_rvtt_sfpmul (b, b, 0);
      c = __builtin_rvtt_sfpadd (c, c, 0);
      d = __builtin_rvtt_sfpmul (d, d, 0);
      a = __builtin_rvtt_sfpmul (a, a, 0);
      b = __builtin_rvtt_sfpmul (b, b, 0);
      c = __builtin_rvtt_sfpadd (c, c, 0);
      d = __builtin_rvtt_sfpmul (d, d, 0);
      a = __builtin_rvtt_sfpmuli (nullptr, a, 0x3c55, 0, 0, 0);
      a = __builtin_rvtt_sfpmul (a, a, 0);
      b = __builtin_rvtt_sfpmul (b, b, 0);
      c = __builtin_rvtt_sfpadd (c, c, 0);
      d = __builtin_rvtt_sfpmul (d, d, 0);
      a = __builtin_rvtt_sfpmul (a, a, 0);
      b = __builtin_rvtt_sfpmul (b, b, 0);
      c = __builtin_rvtt_sfpadd (c, c, 0);
      d = __builtin_rvtt_sfpmul (d, d, 0);
      a = __builtin_rvtt_sfpmul (a, a, 0);
      b = __builtin_rvtt_sfpmul (b, b, 0);
      c = __builtin_rvtt_sfpadd (c, c, 0);
      d = __builtin_rvtt_sfpmul (d, d, 0);
      a = __builtin_rvtt_sfpmuli (nullptr, a, 0x3d63, 0, 0, 0);
    }
  __builtin_rvtt_sfpwritelreg (a, 0);
  __builtin_rvtt_sfpwritelreg (b, 1);
  __builtin_rvtt_sfpwritelreg (c, 2);
  __builtin_rvtt_sfpwritelreg (d, 3);
}
