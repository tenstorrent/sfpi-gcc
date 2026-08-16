// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-hoist -fdump-rtl-rvtt_replay-details" }
// { dg-final { scan-rtl-dump "Hoist profitable: modeled benefit 245 >= 60 .trips 4, length 4." "rvtt_replay" } }
// { dg-final { scan-rtl-dump-times "Hoisted no-exec capture" 1 "rvtt_replay" } }
// { dg-final { scan-rtl-dump "Record delivery hidden: contiguous launch run 3 x length 4 exec surplus 831 >= record delivery 615" "rvtt_replay" } }
// { dg-final { scan-rtl-dump "Not hoisting: modeled benefit -615 < 60 .trips 4, length 4." "rvtt_replay" } }

// Structural saturation boundary, no flag interplay: contiguity here is
// literal (no typed increments at all; sibling groups are separated by
// ordinary delivered payload words with per-site unique immediates, and
// the sibling payload is phase-asymmetric -- mul,mul,add,mul -- so the
// sequence former cannot re-segment contiguous copies into a longer
// period).
//
// run_two: four sibling copies of the four-slot payload in contiguous
// PAIRS.  A run of 2 has execution surplus 2 * (400 - 123) = 554 < 615,
// one delivered word short of hiding the record pass: still
// delivery-bound, fires at the unchanged benefit 4 * (615-400) - 615
// = 245.  This is the just-under-saturation near miss.
void run_two ()
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  for (unsigned ix = 0; ix != 4; ++ix)
    {
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpadd (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpadd (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmuli (nullptr, x, 0x3a12, 0, 0, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpadd (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpadd (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmuli (nullptr, x, 0x3b47, 0, 0, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}

// run_three: six sibling copies in contiguous TRIPLES.  A run of 3 has
// execution surplus 3 * (400 - 123) = 831 >= 615: the record delivery
// hides under the run's execution, the modeled benefit degenerates to
// -615, and the capture stays in the loop as an ordinary
// record-with-execution.
void run_three ()
{
  auto y = __builtin_rvtt_sfpreadlreg (2);
  for (unsigned ix = 0; ix != 4; ++ix)
    {
      y = __builtin_rvtt_sfpmul (y, y, 0);
      y = __builtin_rvtt_sfpmul (y, y, 0);
      y = __builtin_rvtt_sfpadd (y, y, 0);
      y = __builtin_rvtt_sfpmul (y, y, 0);
      y = __builtin_rvtt_sfpmul (y, y, 0);
      y = __builtin_rvtt_sfpmul (y, y, 0);
      y = __builtin_rvtt_sfpadd (y, y, 0);
      y = __builtin_rvtt_sfpmul (y, y, 0);
      y = __builtin_rvtt_sfpmul (y, y, 0);
      y = __builtin_rvtt_sfpmul (y, y, 0);
      y = __builtin_rvtt_sfpadd (y, y, 0);
      y = __builtin_rvtt_sfpmul (y, y, 0);
      y = __builtin_rvtt_sfpmuli (nullptr, y, 0x3c55, 0, 0, 0);
      y = __builtin_rvtt_sfpmul (y, y, 0);
      y = __builtin_rvtt_sfpmul (y, y, 0);
      y = __builtin_rvtt_sfpadd (y, y, 0);
      y = __builtin_rvtt_sfpmul (y, y, 0);
      y = __builtin_rvtt_sfpmul (y, y, 0);
      y = __builtin_rvtt_sfpmul (y, y, 0);
      y = __builtin_rvtt_sfpadd (y, y, 0);
      y = __builtin_rvtt_sfpmul (y, y, 0);
      y = __builtin_rvtt_sfpmul (y, y, 0);
      y = __builtin_rvtt_sfpmul (y, y, 0);
      y = __builtin_rvtt_sfpadd (y, y, 0);
      y = __builtin_rvtt_sfpmul (y, y, 0);
      y = __builtin_rvtt_sfpmuli (nullptr, y, 0x3d63, 0, 0, 0);
    }
  __builtin_rvtt_sfpwritelreg (y, 2);
}
