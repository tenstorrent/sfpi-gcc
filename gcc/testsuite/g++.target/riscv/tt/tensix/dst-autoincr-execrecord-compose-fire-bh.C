// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-dst-autoincr -fdump-rtl-rvtt_dst_autoincr-details" }
// NEVER-hang witness twin of the no-exec-record composition refusal:
// the SAME in-nest placement with the capture recorded WITH execution
// (TTREPLAY load=1 exec=1) keeps firing -- exec-while-record composition
// carries fleet-wide silicon witnesses (minmax, sdpa, where, typecast,
// lcm ON-set in-body re-record), and the guard must key on exec=0,
// never on the presence of replay.
// { dg-final { scan-rtl-dump-not "mod-write-noexec-record-composition-unaudited" "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump "Dst-autoincr group: bb \[0-9\]+ rows 1 stride 2 config 3 words .preheader." "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-not "TTINCRWC" } }
// { dg-final { scan-assembler-times "TTSETC16\t34, 2" 1 } }

using vec_t = __xtt_vector;

void
execrecord_inside_nest ()
{
  for (unsigned face = 0; face != 4; ++face)
    {
      __builtin_rvtt_ttreplay (nullptr, 3, 0, 0, 0, 1, 1);
      auto x = __builtin_rvtt_sfpreadlreg (0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      __builtin_rvtt_sfpwritelreg (x, 0);
      __builtin_rvtt_ttreplay (nullptr, 3, 0, 0, 0, 0, 0);
      __builtin_rvtt_ttreplay (nullptr, 3, 0, 0, 0, 0, 0);

      for (unsigned ix = 0; ix != 11; ++ix)
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
}
