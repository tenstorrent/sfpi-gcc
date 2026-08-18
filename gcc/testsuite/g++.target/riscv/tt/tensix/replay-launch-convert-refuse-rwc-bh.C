// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-invariant-loadi -mtt-tensix-optimize-replay-hoist -mtt-tensix-replay-hoist-min-benefit=0 -fdump-rtl-rvtt_replay-details" }
// Uncovered RWC: the trailing row is isomorphic but its Dst advance differs
// from the uniform advance of the payload's other execution sites.  The
// later Dst auto-increment ownership pass must see equivalent rows at every
// site, so the conversion refuses.
// { dg-final { scan-rtl-dump "Not converting isomorphic run at insn \[0-9\]+: trailing Dst-advance context differs" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "Converted isomorphic run" "rvtt_replay" } }
// { dg-final { scan-assembler-times "TTINCRWC\t0, 4, 0, 0" 1 } }

using vec_t = __xtt_vector;

// Two interleaved dependence chains keep the reissue delivery-bound
// under the corrected pricing (dependence distance two absorbs the
// audited mad-family latency).
#define ROW(SLOT_A, SLOT_B)						\
  vec_t a##SLOT_A = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 0);	\
  vec_t b##SLOT_A = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 2);	\
  vec_t a1##SLOT_A = __builtin_rvtt_sfpmul (a##SLOT_A, c0, 0);		\
  vec_t b1##SLOT_A = __builtin_rvtt_sfpmul (b##SLOT_A, c0, 0);		\
  vec_t a2##SLOT_A = __builtin_rvtt_sfpmul (a1##SLOT_A, c1, 0);		\
  vec_t b2##SLOT_A = __builtin_rvtt_sfpmul (b1##SLOT_A, c1, 0);		\
  vec_t a3##SLOT_A = __builtin_rvtt_sfpmul (a2##SLOT_A, c2, 0);		\
  vec_t b3##SLOT_A = __builtin_rvtt_sfpmul (b2##SLOT_A, c2, 0);		\
  vec_t a4##SLOT_A = __builtin_rvtt_sfpmul (a3##SLOT_A, c3, 0);		\
  vec_t b4##SLOT_A = __builtin_rvtt_sfpmul (b3##SLOT_A, c3, 0);		\
  __builtin_rvtt_sfpstore (nullptr, a4##SLOT_A, 0, 0, 0, 0, 0);		\
  __builtin_rvtt_sfpstore (nullptr, b4##SLOT_A, 0, 0, 0, 0, 2)

void
tail_rows_rwc ()
{
  vec_t c0 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e000001, 0, 0, 31);
  vec_t c1 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e000002, 0, 0, 31);
  vec_t c2 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e000003, 0, 0, 31);
  vec_t c3 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e000004, 0, 0, 31);
  for (unsigned ix = 0; ix != 16; ++ix)
    {
      ROW (r, r);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
  /* Identical row followed by a different Dst advance.  */
  ROW (t, t);
  __builtin_rvtt_ttincrwc (0, 4, 0, 0);
}
