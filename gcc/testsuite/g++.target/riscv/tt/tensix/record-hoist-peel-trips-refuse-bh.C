// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-record-hoist -mtt-tensix-optimize-record-hoist-peel -fdump-rtl-rvtt_replay-details" }
// Named peel refusal: a runtime inner trip count cannot license the
// counter rewrite (the peeled loop must provably run trips-1 times; a
// single-bb loop is do-while, so an unproven or single trip refuses) --
// record-hoist-peel-trips-unproven, the doomed-hoist mirror keeps its
// own refusal, and the in-body exec-record formation is kept
// byte-identically.  The lane FW runtime-trip admission is deliberately
// NOT extended to the peel.
// { dg-final { scan-rtl-dump "record-hoist refused: record-hoist-peel-trips-unproven" "rvtt_replay" } }
// { dg-final { scan-rtl-dump "record-hoist refused: noexec-rerecord-dststore-composition-unaudited" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "record-hoist-peel: exec-recorded" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "Hoisted no-exec capture" "rvtt_replay" } }
static volatile int tile_sink;
void peel_trips_refuse (unsigned faces)
{
  auto a = __builtin_rvtt_sfpreadlreg (0);
  auto b = __builtin_rvtt_sfpreadlreg (1);
  auto c = __builtin_rvtt_sfpreadlreg (2);
  for (unsigned t = 0; t != 3; ++t)		// outer tile-block loop
    {
      for (unsigned ix = 0; ix != faces; ++ix)	// runtime inner bound
	{
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
