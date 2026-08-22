// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-hoist -mtt-tensix-replay-hoist-min-benefit=0 -fdump-rtl-rvtt_replay" }
// Boundary twin of the no-exec re-record sweep (lane FJ): the SAME
// in-loop hoisted-record placement with a STORELESS payload is the
// celu/eqz-class wrapper-record shape, silicon-good across many pins --
// the sweep leaves it untouched and the record plus launches survive.
// Delivery flipped against replay-noexec-rerecord-dststore-unhoist-bh.C:
// only the payload's Dst store separates keep from un-hoist.
// { dg-final { scan-rtl-dump-not "noexec-rerecord-dststore-composition-unaudited" "rvtt_replay" } }
// { dg-final { scan-assembler-times "TTREPLAY\t0, 18, 0, 1" 1 } }
// { dg-final { scan-assembler "TTREPLAY\t0, 18, 0, 0" } }

using vec_t = __xtt_vector;

void
rerecord_storeless_in_outer_loop ()
{
  for (unsigned face = 0; face != 4; ++face)
    for (unsigned ix = 0; ix != 8; ++ix)
      {
	vec_t a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
	vec_t b = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 2, 7);
	vec_t a0 = __builtin_rvtt_sfpmul (a, a, 0);
	vec_t b0 = __builtin_rvtt_sfpmul (b, b, 0);
	vec_t a1 = __builtin_rvtt_sfpmul (a0, a0, 0);
	vec_t b1 = __builtin_rvtt_sfpmul (b0, b0, 0);
	vec_t a2 = __builtin_rvtt_sfpmul (a1, a1, 0);
	vec_t b2 = __builtin_rvtt_sfpmul (b1, b1, 0);
	vec_t a3 = __builtin_rvtt_sfpmul (a2, a2, 0);
	vec_t b3 = __builtin_rvtt_sfpmul (b2, b2, 0);
	vec_t a4 = __builtin_rvtt_sfpmul (a3, a3, 0);
	vec_t b4 = __builtin_rvtt_sfpmul (b3, b3, 0);
	vec_t a5 = __builtin_rvtt_sfpmul (a4, a4, 0);
	vec_t b5 = __builtin_rvtt_sfpmul (b4, b4, 0);
	vec_t a6 = __builtin_rvtt_sfpmul (a5, a5, 0);
	vec_t b6 = __builtin_rvtt_sfpmul (b5, b5, 0);
	a6 = __builtin_rvtt_sfpmul (a6, b5, 0);
	b6 = __builtin_rvtt_sfpmul (b6, a5, 0);
	__builtin_rvtt_sfpwritelreg (a6, 0);
	__builtin_rvtt_sfpwritelreg (b6, 1);
	__builtin_rvtt_ttincrwc (0, 4, 0, 0);
      }
}
