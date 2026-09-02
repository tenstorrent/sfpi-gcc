// SFPIADD immediate-form effect/latency audit (rvtt-cost.md
// D3-follow-up row): the constant-immediate SFPIADD alternatives carry
// audited effects (reads VC + the tied live-in, lane-predicated VD
// write, CC per mod1, Simple-unit result latency 0), so a counted
// payload consuming their results is priceable.  The body is the
// established delivery-bound fire-counted shape with the c chain's
// multiplies replaced by immediate adds -- the pricing arithmetic is
// byte-identical to replay-hoist-profit-fire-counted-bh.C, proving the
// audit adds no phantom stalls.  Before this audit the identical body
// refused replay-reissue-latency-unproved (the effect-opaque producer
// was this pattern -- the actual blocker of the fresh-body ceil and
// rewritten-lcm loops, NOT SFPSWAP, whose audit predates this one).
// The twin varies names, immediates, registers, and the CC-effect mod
// within the audited envelope.
// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-hoist -fdump-rtl-rvtt_replay-details" }
// { dg-final { scan-rtl-dump-times "Hoist pricing .loop \\d+.: trips 32, words 8, exec_ilk 8 slots, deliver_body 984, deliver_record 1107, record 1407, before 984, after 870, benefit 2241 .min 60." 2 "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "replay-reissue-latency-unproved" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-times "Hoisted no-exec capture" 2 "rvtt_replay" } }
// { dg-final { scan-assembler-times "TTREPLAY" 66 } }

void iadd_imm_fire ()
{
  auto a = __builtin_rvtt_sfpreadlreg (0);
  auto b = __builtin_rvtt_sfpreadlreg (1);
  auto c = __builtin_rvtt_sfpreadlreg (2);
  auto d = __builtin_rvtt_sfpreadlreg (3);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      a = __builtin_rvtt_sfpmul (a, a, 0);
      b = __builtin_rvtt_sfpmul (b, b, 0);
      c = __builtin_rvtt_sfpiadd_i (nullptr, c, 23, 0, 0, 4);
      d = __builtin_rvtt_sfpmul (d, d, 0);
      a = __builtin_rvtt_sfpmul (a, a, 0);
      b = __builtin_rvtt_sfpmul (b, b, 0);
      c = __builtin_rvtt_sfpiadd_i (nullptr, c, -9, 0, 0, 4);
      d = __builtin_rvtt_sfpmul (d, d, 0);
    }
  __builtin_rvtt_sfpwritelreg (a, 0);
  __builtin_rvtt_sfpwritelreg (b, 1);
  __builtin_rvtt_sfpwritelreg (c, 2);
  __builtin_rvtt_sfpwritelreg (d, 3);
}

void renamed_varied_bias (void)
{
  auto north = __builtin_rvtt_sfpreadlreg (4);
  auto south = __builtin_rvtt_sfpreadlreg (5);
  auto east = __builtin_rvtt_sfpreadlreg (6);
  auto west = __builtin_rvtt_sfpreadlreg (7);
  for (unsigned step = 0; step != 32; ++step)
    {
      north = __builtin_rvtt_sfpmul (north, north, 0);
      south = __builtin_rvtt_sfpmul (south, south, 0);
      east = __builtin_rvtt_sfpiadd_i (nullptr, east, 0x155, 0, 0, 4);
      west = __builtin_rvtt_sfpmul (west, west, 0);
      north = __builtin_rvtt_sfpmul (north, north, 0);
      south = __builtin_rvtt_sfpmul (south, south, 0);
      east = __builtin_rvtt_sfpiadd_i (nullptr, east, -0x2a, 0, 0, 4);
      west = __builtin_rvtt_sfpmul (west, west, 0);
    }
  __builtin_rvtt_sfpwritelreg (north, 4);
  __builtin_rvtt_sfpwritelreg (south, 5);
  __builtin_rvtt_sfpwritelreg (east, 6);
  __builtin_rvtt_sfpwritelreg (west, 7);
}
