// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-dump-effects" }
// Layer-1 effect-set goldens for the SFPIADD / constant-LREG SFPSWAP /
// per-mod SFPCAST audit (effect-attributes lane).  Sources of truth:
// SFPIADD.md / SFPSWAP.md / SFPCAST.md functional models and craq-sim
// TENSIX_EXECUTE_{SFPIADD,SFPSWAP,SFPCAST}.
//
// SFPIADD mod1=4 (CC_NONE): reads VC+VB, writes VD, lane-predicated only.
// { dg-final { scan-assembler-times {# xtt-effects: subunit=simple latency=-1 lreg-read=0x3 lreg-write=0x1 port=shared_simple_round cc=r config=0x0 rwc=none dst=none encodable=no} 1 } }
// SFPIADD mod1=0 (CC_LT0): same data effects plus a CC write.
// { dg-final { scan-assembler-times {# xtt-effects: subunit=simple latency=-1 lreg-read=0x3 lreg-write=0x1 port=shared_simple_round cc=rw config=0x0 rwc=none dst=none encodable=no} 1 } }
// BH SFPCAST mod1=3 (self-inverse sign-magnitude negate): audited.
// { dg-final { scan-assembler-times {# xtt-effects: subunit=simple latency=-1 lreg-read=0x1 lreg-write=0x1 port=shared_simple_round cc=r config=0x0 rwc=none dst=none encodable=no} 1 } }
// Constant-LREG SFPSWAP (both cst1 with the constant in the VC position
// and cst2 with it in the VD position): the write to the constant LREG
// (>= L8) is architecturally dropped, leaving one allocatable write.
// { dg-final { scan-assembler-times {# xtt-effects: subunit=simple latency=-1 lreg-read=0x1 lreg-write=0x1 port=borrows_mad cc=r config=0x0 rwc=none dst=none encodable=yes} 2 } }
// Unproven SFPCAST mods (1 = stochastic/PRNG, 2 = BH cast-as-ABS bug)
// and the unaudited SFPABS neighbor keep the refusing default.
// { dg-final { scan-assembler-times {# xtt-effects: opaque} 3 } }

__attribute__((noinline)) void iadd_cc_none_effects ()
{
  __builtin_rvtt_sfppushc (0);
  __builtin_rvtt_sfppopc (0);
  auto a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 4, 7);
  auto b = __builtin_rvtt_sfpload (nullptr, 64, 0, 0, 4, 7);
  auto s = __builtin_rvtt_sfpiadd_v (b, a, 4);		/* CC_NONE */
  auto sm = __builtin_rvtt_sfpcast (s, 3);		/* BH mod3 */
  __builtin_rvtt_sfpstore (nullptr, sm, 0, 0, 0, 4, 7);
}

__attribute__((noinline)) void iadd_cc_write_effects ()
{
  __builtin_rvtt_sfppushc (0);
  auto a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 4, 7);
  auto b = __builtin_rvtt_sfpload (nullptr, 64, 0, 0, 4, 7);
  auto s = __builtin_rvtt_sfpiadd_v (b, a, 0);		/* CC_LT0 */
  __builtin_rvtt_sfpstore (nullptr, s, 0, 0, 0, 4, 7);
  __builtin_rvtt_sfppopc (0);
}

__attribute__((noinline)) void swap_cst_effects ()
{
  __builtin_rvtt_sfppushc (0);
  __builtin_rvtt_sfppopc (0);
  auto v = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
  auto zero = __builtin_rvtt_sfpreadlreg (9);
  auto lo = __builtin_rvtt_sfpswap (v, zero, 1);	/* cst VC: *_cst1 */
  auto mn = __builtin_rvtt_sfpselect2 (lo, 0);
  __builtin_rvtt_sfpstore (nullptr, mn, 0, 0, 0, 0, 7);
  auto w = __builtin_rvtt_sfpload (nullptr, 64, 0, 0, 0, 7);
  auto one = __builtin_rvtt_sfpreadlreg (10);
  auto hi = __builtin_rvtt_sfpswap (one, w, 1);		/* cst VD: *_cst2 */
  auto mx = __builtin_rvtt_sfpselect2 (hi, 1);
  __builtin_rvtt_sfpstore (nullptr, mx, 128, 0, 0, 0, 7);
}

__attribute__((noinline)) void cast_unproven_mods_opaque ()
{
  __builtin_rvtt_sfppushc (0);
  __builtin_rvtt_sfppopc (0);
  auto a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 4, 7);
  auto c1 = __builtin_rvtt_sfpcast (a, 1);		/* stochastic */
  auto c2 = __builtin_rvtt_sfpcast (c1, 2);		/* ABS bug */
  auto keep = __builtin_rvtt_sfpabs (c2, 1);		/* unaudited */
  __builtin_rvtt_sfpstore (nullptr, keep, 0, 0, 0, 4, 7);
}
