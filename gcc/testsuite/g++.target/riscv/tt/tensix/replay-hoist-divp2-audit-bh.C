// SFPDIVP2 effect/latency audit (rvtt-cost.md D3-follow-up
// row): the constant-immediate SFPDIVP2 alternatives carry audited
// effects (reads VC + the tied live-in, lane-predicated VD write, no
// lane-flag effect, Simple-unit result latency 0), so a counted
// payload consuming their results is priceable -- the fire-counted
// arithmetic with the c chain's multiplies replaced by exponent
// adjustments.  Before this audit the identical body refused
// replay-reissue-latency-unproved (the fresh-body rsqrt loop's
// blocker).  The twin varies names, immediates, registers, and the
// exponent-set mod 0 within the audited envelope.
// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-hoist -fdump-rtl-rvtt_replay-details" }
// { dg-final { scan-rtl-dump-times "Hoist pricing .loop \\d+.: trips 32, words 8, exec_ilk 8 slots, deliver_body 984, deliver_record 1107, record 1407, before 984, after 870, benefit 2241 .min 60." 2 "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "replay-reissue-latency-unproved" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-times "Hoisted no-exec capture" 2 "rvtt_replay" } }
// { dg-final { scan-assembler-times "TTREPLAY" 66 } }

void divp2_fire ()
{
  auto a = __builtin_rvtt_sfpreadlreg (0);
  auto b = __builtin_rvtt_sfpreadlreg (1);
  auto c = __builtin_rvtt_sfpreadlreg (2);
  auto d = __builtin_rvtt_sfpreadlreg (3);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      a = __builtin_rvtt_sfpmul (a, a, 0);
      b = __builtin_rvtt_sfpmul (b, b, 0);
      c = __builtin_rvtt_sfpdivp2 (nullptr, c, -1, 0, 0, 1);
      d = __builtin_rvtt_sfpmul (d, d, 0);
      a = __builtin_rvtt_sfpmul (a, a, 0);
      b = __builtin_rvtt_sfpmul (b, b, 0);
      c = __builtin_rvtt_sfpdivp2 (nullptr, c, 2, 0, 0, 1);
      d = __builtin_rvtt_sfpmul (d, d, 0);
    }
  __builtin_rvtt_sfpwritelreg (a, 0);
  __builtin_rvtt_sfpwritelreg (b, 1);
  __builtin_rvtt_sfpwritelreg (c, 2);
  __builtin_rvtt_sfpwritelreg (d, 3);
}

void renamed_varied_scalebits (void)
{
  auto north = __builtin_rvtt_sfpreadlreg (4);
  auto south = __builtin_rvtt_sfpreadlreg (5);
  auto east = __builtin_rvtt_sfpreadlreg (6);
  auto west = __builtin_rvtt_sfpreadlreg (7);
  for (unsigned step = 0; step != 32; ++step)
    {
      north = __builtin_rvtt_sfpmul (north, north, 0);
      south = __builtin_rvtt_sfpmul (south, south, 0);
      east = __builtin_rvtt_sfpdivp2 (nullptr, east, 0x7f, 0, 0, 0);
      west = __builtin_rvtt_sfpmul (west, west, 0);
      north = __builtin_rvtt_sfpmul (north, north, 0);
      south = __builtin_rvtt_sfpmul (south, south, 0);
      east = __builtin_rvtt_sfpdivp2 (nullptr, east, 3, 0, 0, 1);
      west = __builtin_rvtt_sfpmul (west, west, 0);
    }
  __builtin_rvtt_sfpwritelreg (north, 4);
  __builtin_rvtt_sfpwritelreg (south, 5);
  __builtin_rvtt_sfpwritelreg (east, 6);
  __builtin_rvtt_sfpwritelreg (west, 7);
}
