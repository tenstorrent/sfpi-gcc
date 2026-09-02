// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-record-hoist -fdump-rtl-rvtt_replay-details" }
// Multi-block runtime tile loop (the MEASURED profiler-vehicle
// shape): buffer-management control flow splits the tile loop into
// several blocks around an always-executed body.  The capture bb
// dominates the loop latch (per-trip execution is structural), the
// replay-preservation audit walks EVERY block of the loop, and the
// record hoists to the dedicated preheader.
// { dg-final { scan-rtl-dump "record-hoist: multi-block loop \\d+ admitted .capture bb \\d+ dominates latch bb \\d+." "rvtt_replay" } }
// { dg-final { scan-rtl-dump "record-hoist: runtime-trip re-record window admitted .structural trips>=1, words 6," "rvtt_replay" } }
// { dg-final { scan-rtl-dump-times "Hoisted no-exec capture" 1 "rvtt_replay" } }
// { dg-final { scan-assembler-times "TTREPLAY\t0, 6, 0, 1" 1 } }
// { dg-final { scan-assembler-times "TTREPLAY\t0, 6, 0, 0" 2 } }
static volatile int mb_ticks;
void rerecord_multibb_tile_loop (unsigned n)
{
  auto a = __builtin_rvtt_sfpreadlreg (0);
  auto b = __builtin_rvtt_sfpreadlreg (1);
  auto c = __builtin_rvtt_sfpreadlreg (2);
  volatile unsigned *fifo = (volatile unsigned *) 0xFFE40000u;
  for (unsigned ix = 0; ix != n; ++ix)
    {
      if (ix & 2)		// profiler-style side path: splits the loop
	mb_ticks = (int) ix;
      a = __builtin_rvtt_sfpmul (a, a, 0);
      b = __builtin_rvtt_sfpmul (b, b, 0);
      c = __builtin_rvtt_sfpmul (c, c, 0);
      a = __builtin_rvtt_sfpmul (a, b, 0);
      b = __builtin_rvtt_sfpmul (b, c, 0);
      c = __builtin_rvtt_sfpmul (c, a, 0);
      *fifo = 0xB2010000u + ((ix & 1u) << 9);
      a = __builtin_rvtt_sfpmul (a, a, 0);
      b = __builtin_rvtt_sfpmul (b, b, 0);
      c = __builtin_rvtt_sfpmul (c, c, 0);
      a = __builtin_rvtt_sfpmul (a, b, 0);
      b = __builtin_rvtt_sfpmul (b, c, 0);
      c = __builtin_rvtt_sfpmul (c, a, 0);
    }
  __builtin_rvtt_sfpwritelreg (a, 0);
  __builtin_rvtt_sfpwritelreg (b, 1);
  __builtin_rvtt_sfpwritelreg (c, 2);
}
