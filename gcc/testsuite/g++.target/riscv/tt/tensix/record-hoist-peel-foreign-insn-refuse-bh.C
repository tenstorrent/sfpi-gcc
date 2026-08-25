// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-record-hoist -mtt-tensix-optimize-record-hoist-peel -fdump-rtl-rvtt_replay-details" }
// Named peel refusal: a body word outside the clone spans (here the
// volatile scalar separator between the two row units) would be
// silently dropped from the peeled first trip -- the peel refuses by
// record-hoist-peel-body-foreign-insn, the doomed-hoist mirror keeps
// its own refusal, and the in-body exec-record formation is kept
// byte-identically.
// { dg-final { scan-rtl-dump "record-hoist refused: record-hoist-peel-body-foreign-insn" "rvtt_replay" } }
// { dg-final { scan-rtl-dump "record-hoist refused: noexec-rerecord-dststore-composition-unaudited" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "record-hoist-peel: exec-recorded" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "Hoisted no-exec capture" "rvtt_replay" } }
// { dg-final { scan-rtl-dump "Capturing and executing sequence" "rvtt_replay" } }
static volatile int dstore_sink;
void peel_foreign_insn_refuse ()
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
	  dstore_sink = 7;	// foreign scalar word between clones
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
