// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-record-hoist -fdump-rtl-rvtt_replay-details" }
// Named refusal (lane FW, doomed-hoist mirror): a Dst-store payload
// whose no-exec record would land in a preheader INSIDE an outer loop
// is exactly the shape the fail-closed re-record sweep un-hoists
// (lane FJ) -- and the un-hoist inlines every launch, a strict delivery
// pessimization.  The admission refuses by the sweep's own name and the
// in-body exec-record formation is kept byte-identically.
// { dg-final { scan-rtl-dump "record-hoist refused: noexec-rerecord-dststore-composition-unaudited" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "Hoisted no-exec capture" "rvtt_replay" } }
// { dg-final { scan-rtl-dump "Capturing and executing sequence" "rvtt_replay" } }
static volatile int dstore_sink;
void rerecord_dststore_inner_loop ()
{
  auto a = __builtin_rvtt_sfpreadlreg (0);
  auto b = __builtin_rvtt_sfpreadlreg (1);
  for (unsigned t = 0; t != 3; ++t)		// outer tile-block loop
    {
      for (unsigned ix = 0; ix != 4; ++ix)	// inner tile loop
	{
	  a = __builtin_rvtt_sfpmul (a, b, 0);
	  b = __builtin_rvtt_sfpmul (b, b, 0);
	  a = __builtin_rvtt_sfpmul (a, a, 0);
	  __builtin_rvtt_sfpstore (nullptr, a, 0, 0, 0, 0, 6);
	  dstore_sink = (int) ix;
	  a = __builtin_rvtt_sfpmul (a, b, 0);
	  b = __builtin_rvtt_sfpmul (b, b, 0);
	  a = __builtin_rvtt_sfpmul (a, a, 0);
	  __builtin_rvtt_sfpstore (nullptr, a, 0, 0, 0, 0, 6);
	}
      dstore_sink = (int) t;
    }
  __builtin_rvtt_sfpwritelreg (a, 0);
  __builtin_rvtt_sfpwritelreg (b, 1);
}
