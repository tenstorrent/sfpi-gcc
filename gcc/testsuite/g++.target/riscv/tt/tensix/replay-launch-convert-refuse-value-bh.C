// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-invariant-loadi -mtt-tensix-optimize-replay-hoist -fdump-rtl-rvtt_replay-details" }
// Near miss: the trailing row's multiply chain feeds back a different value
// (t3*t3 instead of t3*t1).  The lockstep value map fails and the row must
// stay inline; the loop's own renamed final copy still converts.
// { dg-final { scan-rtl-dump-times "Converted isomorphic run" 1 "rvtt_replay" } }
// { dg-final { scan-assembler-times "SFPMUL" 10 } }

using vec_t = __xtt_vector;

void
tail_rows_diff ()
{
  for (unsigned ix = 0; ix != 7; ++ix)
    {
      vec_t a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      vec_t c0 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e000001, 0, 0, 31);
      vec_t c1 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e000002, 0, 0, 31);
      vec_t c2 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e000003, 0, 0, 31);
      vec_t c3 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e000004, 0, 0, 31);
      vec_t t1 = __builtin_rvtt_sfpmul (a, c0, 0);
      vec_t t2 = __builtin_rvtt_sfpmul (t1, c1, 0);
      vec_t t3 = __builtin_rvtt_sfpmul (t2, c2, 0);
      vec_t t4 = __builtin_rvtt_sfpmul (t3, t1, 0);
      vec_t t5 = __builtin_rvtt_sfpmul (t4, c3, 0);
      __builtin_rvtt_sfpstore (nullptr, t5, 0, 0, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
  vec_t a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
  vec_t c0 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e000001, 0, 0, 31);
  vec_t c1 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e000002, 0, 0, 31);
  vec_t c2 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e000003, 0, 0, 31);
  vec_t c3 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e000004, 0, 0, 31);
  vec_t t1 = __builtin_rvtt_sfpmul (a, c0, 0);
  vec_t t2 = __builtin_rvtt_sfpmul (t1, c1, 0);
  vec_t t3 = __builtin_rvtt_sfpmul (t2, c2, 0);
  vec_t t4 = __builtin_rvtt_sfpmul (t3, t3, 0);
  vec_t t5 = __builtin_rvtt_sfpmul (t4, c3, 0);
  __builtin_rvtt_sfpstore (nullptr, t5, 0, 0, 0, 0, 7);
  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
}
