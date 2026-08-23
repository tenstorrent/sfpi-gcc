// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-record-hoist -fdump-rtl-rvtt_replay-details" }
// Multi-record calendar (lane FW; the blaze sdpa_reduce_row shape has
// exactly this structure): TWO distinct invariant windows re-record in
// the same runtime-trip tile loop.  The first hoist leaves its playback
// launches in the loop; the second window's replay-preservation audit
// must ADMIT those launches -- they are this pass's own, their recorded
// content is the pass's audited payload (a user launch refuses, see
// record-hoist-owner-refuse-bh.C) -- and hoist alongside into disjoint
// slots.  Both windows are 6 words: 2-trip benefit 175 each.
// { dg-final { scan-rtl-dump-times "record-hoist: runtime-trip re-record window admitted" 2 "rvtt_replay" } }
// { dg-final { scan-rtl-dump-times "Hoisted no-exec capture" 2 "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "Capturing and executing sequence" "rvtt_replay" } }
// { dg-final { scan-assembler-times "TTREPLAY\t0, 6, 0, 1" 1 } }
// { dg-final { scan-assembler-times "TTREPLAY\t6, 6, 0, 1" 1 } }
// { dg-final { scan-assembler-times "TTREPLAY\t0, 6, 0, 0" 2 } }
// { dg-final { scan-assembler-times "TTREPLAY\t6, 6, 0, 0" 2 } }
void rerecord_two_windows (unsigned n)
{
  auto a = __builtin_rvtt_sfpreadlreg (0);
  auto b = __builtin_rvtt_sfpreadlreg (1);
  auto c = __builtin_rvtt_sfpreadlreg (2);
  volatile unsigned *fifo = (volatile unsigned *) 0xFFE40000u;
  for (unsigned ix = 0; ix != n; ++ix)
    {
      // Window A, clone 1.
      a = __builtin_rvtt_sfpmul (a, a, 0);
      b = __builtin_rvtt_sfpmul (b, b, 0);
      c = __builtin_rvtt_sfpmul (c, c, 0);
      a = __builtin_rvtt_sfpmul (a, b, 0);
      b = __builtin_rvtt_sfpmul (b, c, 0);
      c = __builtin_rvtt_sfpmul (c, a, 0);
      *fifo = 0xB2010000u + ((ix & 1u) << 9);
      // Window A, clone 2.
      a = __builtin_rvtt_sfpmul (a, a, 0);
      b = __builtin_rvtt_sfpmul (b, b, 0);
      c = __builtin_rvtt_sfpmul (c, c, 0);
      a = __builtin_rvtt_sfpmul (a, b, 0);
      b = __builtin_rvtt_sfpmul (b, c, 0);
      c = __builtin_rvtt_sfpmul (c, a, 0);
      *fifo = 0xB2018000u + ((ix & 1u) << 9);
      // Window B, clone 1 (distinct encodings: operand roles swapped).
      b = __builtin_rvtt_sfpmul (b, a, 0);
      c = __builtin_rvtt_sfpmul (c, b, 0);
      a = __builtin_rvtt_sfpmul (a, c, 0);
      b = __builtin_rvtt_sfpmul (b, b, 0);
      c = __builtin_rvtt_sfpmul (c, c, 0);
      a = __builtin_rvtt_sfpmul (a, a, 0);
      *fifo = 0xB2010000u + ((ix & 1u) << 9);
      // Window B, clone 2.
      b = __builtin_rvtt_sfpmul (b, a, 0);
      c = __builtin_rvtt_sfpmul (c, b, 0);
      a = __builtin_rvtt_sfpmul (a, c, 0);
      b = __builtin_rvtt_sfpmul (b, b, 0);
      c = __builtin_rvtt_sfpmul (c, c, 0);
      a = __builtin_rvtt_sfpmul (a, a, 0);
    }
  __builtin_rvtt_sfpwritelreg (a, 0);
  __builtin_rvtt_sfpwritelreg (b, 1);
  __builtin_rvtt_sfpwritelreg (c, 2);
}
