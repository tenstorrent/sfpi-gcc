// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-record-hoist -mtt-tensix-optimize-record-hoist-peel -fdump-rtl-rvtt_replay-details" }
// Check that the first-trip peel moves one trip to the preheader with an exec-while-record capture for a Dst-store window in a nested loop.
// { dg-final { scan-rtl-dump "record-hoist-peel: dststore composition rescued by exec-while-record first-trip peel" "rvtt_replay" } }
// { dg-final { scan-rtl-dump "record-hoist-peel: exec-recorded" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "noexec-rerecord-dststore-composition-unaudited" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "Hoisted no-exec capture" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "Replay refusal: noexec-rerecord-dststore" "rvtt_replay" } }
// { dg-final { scan-assembler-times "TTREPLAY\t0, 8, 1, 1" 1 } }
// { dg-final { scan-assembler-times "TTREPLAY\t0, 8, 0, 0" 3 } }
static volatile int tile_sink;
void peel_dststore_inner_loop ()
{
  auto a = __builtin_rvtt_sfpreadlreg (0);
  auto b = __builtin_rvtt_sfpreadlreg (1);
  auto c = __builtin_rvtt_sfpreadlreg (2);
  for (unsigned t = 0; t != 3; ++t)		// outer tile-block loop
    {
      for (unsigned ix = 0; ix != 4; ++ix)	// inner face loop
	{
	  // Two identical back-to-back 8-word row units (no scalar
	  // separator: the peel needs full body coverage), found as one
	  // 8-word window with two clones per trip.
	  a = __builtin_rvtt_sfpmul (a, b, 0);
	  b = __builtin_rvtt_sfpmul (b, c, 0);
	  c = __builtin_rvtt_sfpmul (c, c, 0);
	  a = __builtin_rvtt_sfpmul (a, a, 0);
	  b = __builtin_rvtt_sfpmul (b, a, 0);
	  c = __builtin_rvtt_sfpmul (c, b, 0);
	  a = __builtin_rvtt_sfpmul (a, c, 0);
	  __builtin_rvtt_sfpstore (nullptr, a, 0, 0, 0, 0, 6);
	  a = __builtin_rvtt_sfpmul (a, b, 0);
	  b = __builtin_rvtt_sfpmul (b, c, 0);
	  c = __builtin_rvtt_sfpmul (c, c, 0);
	  a = __builtin_rvtt_sfpmul (a, a, 0);
	  b = __builtin_rvtt_sfpmul (b, a, 0);
	  c = __builtin_rvtt_sfpmul (c, b, 0);
	  a = __builtin_rvtt_sfpmul (a, c, 0);
	  __builtin_rvtt_sfpstore (nullptr, a, 0, 0, 0, 0, 6);
	}
      tile_sink = (int) t;
    }
  __builtin_rvtt_sfpwritelreg (a, 0);
  __builtin_rvtt_sfpwritelreg (b, 1);
  __builtin_rvtt_sfpwritelreg (c, 2);
}
