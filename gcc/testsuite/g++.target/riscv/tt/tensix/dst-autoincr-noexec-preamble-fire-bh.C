// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-dst-autoincr -fdump-rtl-rvtt_dst_autoincr-details" }
// Check that a no-exec capture in the preamble dominating the loop nest does not block the group.
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
