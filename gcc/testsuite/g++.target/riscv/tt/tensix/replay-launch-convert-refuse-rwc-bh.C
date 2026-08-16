// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-invariant-loadi -mtt-tensix-optimize-replay-hoist -fdump-rtl-rvtt_replay-details" }
// Uncovered RWC: the trailing row is isomorphic but its Dst advance differs
// from the uniform advance of the payload's other execution sites.  The
// later Dst auto-increment ownership pass must see equivalent rows at every
// site, so the conversion refuses.
// { dg-final { scan-rtl-dump "Not converting isomorphic run at insn \[0-9\]+: trailing Dst-advance context differs" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "Converted isomorphic run" "rvtt_replay" } }
// { dg-final { scan-assembler-times "TTINCRWC\t0, 4, 0, 0" 1 } }

using vec_t = __xtt_vector;

void
tail_rows_rwc ()
{
  vec_t c0 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e000001, 0, 0, 31);
  vec_t c1 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e000002, 0, 0, 31);
  vec_t c2 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e000003, 0, 0, 31);
  vec_t c3 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e000004, 0, 0, 31);
  for (unsigned ix = 0; ix != 7; ++ix)
    {
      vec_t a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      vec_t t1 = __builtin_rvtt_sfpmul (a, c0, 0);
      vec_t t2 = __builtin_rvtt_sfpmul (t1, c1, 0);
      vec_t t3 = __builtin_rvtt_sfpmul (t2, c2, 0);
      vec_t t4 = __builtin_rvtt_sfpmul (t3, t1, 0);
      vec_t t5 = __builtin_rvtt_sfpmul (t4, c3, 0);
      __builtin_rvtt_sfpstore (nullptr, t5, 0, 0, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
  /* Identical row followed by a different Dst advance.  */
  vec_t a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
  vec_t t1 = __builtin_rvtt_sfpmul (a, c0, 0);
  vec_t t2 = __builtin_rvtt_sfpmul (t1, c1, 0);
  vec_t t3 = __builtin_rvtt_sfpmul (t2, c2, 0);
  vec_t t4 = __builtin_rvtt_sfpmul (t3, t1, 0);
  vec_t t5 = __builtin_rvtt_sfpmul (t4, c3, 0);
  __builtin_rvtt_sfpstore (nullptr, t5, 0, 0, 0, 0, 7);
  __builtin_rvtt_ttincrwc (0, 4, 0, 0);
}
