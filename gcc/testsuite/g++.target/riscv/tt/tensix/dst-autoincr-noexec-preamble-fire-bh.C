// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-dst-autoincr -fdump-rtl-rvtt_dst_autoincr-details" }
// Silicon PASS witness twin (xielu-fresh ON-set class): a no-exec replay
// capture in the loop-free preamble DOMINATING the group's loop nest
// executes once before any group store can be in flight -- the guard
// admits it and the group keeps firing (device-witnessed composition:
// xielu-fresh carries record 0,4,0,1 and a fired group in one function
// and passes correctness and perf on hardware).
// { dg-final { scan-rtl-dump-not "mod-write-noexec-record-composition-unaudited" "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump "Dst-autoincr group: bb \[0-9\]+ rows 1 stride 2 config 3 words .preheader." "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-not "TTINCRWC" } }
// { dg-final { scan-assembler-times "TTSETC16\t34, 2" 1 } }

using vec_t = __xtt_vector;

void
noexec_record_preamble_then_rows ()
{
  // Recorded once, outside every loop, dominating the nest.
  __builtin_rvtt_ttreplay (nullptr, 3, 0, 0, 0, 0, 1);
  auto x = __builtin_rvtt_sfpreadlreg (0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  __builtin_rvtt_sfpwritelreg (x, 0);

  for (unsigned face = 0; face != 4; ++face)
    for (unsigned ix = 0; ix != 8; ++ix)
      {
	vec_t a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
	vec_t p = __builtin_rvtt_sfpmul (a, a, 0);
	p = __builtin_rvtt_sfpmul (p, a, 0);
	p = __builtin_rvtt_sfpmul (p, a, 0);
	p = __builtin_rvtt_sfpmul (p, a, 0);
	p = __builtin_rvtt_sfpmul (p, a, 0);
	p = __builtin_rvtt_sfpmul (p, a, 0);
	__builtin_rvtt_sfpstore (nullptr, p, 0, 0, 0, 0, 7);
	__builtin_rvtt_ttincrwc (0, 2, 0, 0);
      }
}
