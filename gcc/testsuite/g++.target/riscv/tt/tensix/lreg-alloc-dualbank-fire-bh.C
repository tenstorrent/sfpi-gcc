// The IRA dual-bank coloring gap (lane FU): the relational
// exact-register alternatives of rvtt_sfpswap_indexed_int
// (companion == value + 4, twelve ordered pairs) are invisible to
// IRA's per-operand alternative cost scan, so IRA can color the webs
// into a combination no alternative admits; LRA's repair reloads need
// a free LREG, and at peak pressure 8 there is none -- this function
// is a hard lreg-pressure-exceeded error without the binding layer
// (reversed-operand-order swaps with dead companion results seed the
// conflicting constraint-copy preferences; distilled from the lane-EX
// generic_moe_gate_topk top16 reproducer).  Under
// -mtt-tensix-optimize-lreg-alloc the dual-bank pinned-chain binding
// solves alternative selection + coloring, proves same-register
// disjointness point-wise, rewrites the pinned webs to hard LREGs,
// and the function COMPILES with zero repair moves.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-lreg-alloc -fdump-rtl-rvtt_lp_alloc-details" }
// { dg-final { scan-rtl-dump "dual-bank pinned-chain binding" "rvtt_lp_alloc" } }
// { dg-final { scan-rtl-dump "rewritten to hard LREGs" "rvtt_lp_alloc" } }
// { dg-final { scan-assembler {\mSFPSWAP\tL[0-3], L[0-3], 1\t# INDEXED} } }
// { dg-final { scan-assembler-not {\mSFPMOV\M} } }

void dualbank_fire (void)
{
  auto v0 = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
  auto v1 = __builtin_rvtt_sfpload (nullptr, 4, 0, 0, 0, 7);
  auto v2 = __builtin_rvtt_sfpload (nullptr, 8, 0, 0, 0, 7);
  auto v3 = __builtin_rvtt_sfpload (nullptr, 12, 0, 0, 0, 7);
  auto c0 = __builtin_rvtt_sfpload (nullptr, 16, 0, 0, 0, 7);
  auto c1 = __builtin_rvtt_sfpload (nullptr, 20, 0, 0, 0, 7);
  auto c2 = __builtin_rvtt_sfpload (nullptr, 24, 0, 0, 0, 7);
  auto c3 = __builtin_rvtt_sfpload (nullptr, 28, 0, 0, 0, 7);
  auto s1 = __builtin_rvtt_sfpswap_indexed (v0, v2, c0, c2, 1);
  v0 = __builtin_rvtt_sfpselect4 (s1, 0);
  v2 = __builtin_rvtt_sfpselect4 (s1, 1);
  c0 = __builtin_rvtt_sfpselect4 (s1, 2);
  c2 = __builtin_rvtt_sfpselect4 (s1, 3);
  auto s2 = __builtin_rvtt_sfpswap_indexed (v1, v3, c1, c3, 1);
  v1 = __builtin_rvtt_sfpselect4 (s2, 0);
  v3 = __builtin_rvtt_sfpselect4 (s2, 1);
  c1 = __builtin_rvtt_sfpselect4 (s2, 2);
  c3 = __builtin_rvtt_sfpselect4 (s2, 3);
  __builtin_rvtt_sfpstore (nullptr, v0, 32, 0, 0, 0, 7);
  __builtin_rvtt_sfpstore (nullptr, v1, 36, 0, 0, 0, 7);
  __builtin_rvtt_sfpstore (nullptr, v2, 40, 0, 0, 0, 7);
  __builtin_rvtt_sfpstore (nullptr, v3, 44, 0, 0, 0, 7);
  __builtin_rvtt_sfpstore (nullptr, c0, 48, 0, 0, 0, 7);
  __builtin_rvtt_sfpstore (nullptr, c1, 52, 0, 0, 0, 7);
  __builtin_rvtt_sfpstore (nullptr, c2, 56, 0, 0, 0, 7);
  __builtin_rvtt_sfpstore (nullptr, c3, 60, 0, 0, 0, 7);
  v0 = __builtin_rvtt_sfpload (nullptr, 64, 0, 0, 0, 7);
  v1 = __builtin_rvtt_sfpload (nullptr, 68, 0, 0, 0, 7);
  v2 = __builtin_rvtt_sfpload (nullptr, 72, 0, 0, 0, 7);
  v3 = __builtin_rvtt_sfpload (nullptr, 76, 0, 0, 0, 7);
  c0 = __builtin_rvtt_sfpload (nullptr, 80, 0, 0, 0, 7);
  c1 = __builtin_rvtt_sfpload (nullptr, 84, 0, 0, 0, 7);
  c2 = __builtin_rvtt_sfpload (nullptr, 88, 0, 0, 0, 7);
  c3 = __builtin_rvtt_sfpload (nullptr, 92, 0, 0, 0, 7);
  /* Reversed operand order + dead companion results: the shapes that
     seed IRA with conflicting constraint-copy preferences.  */
  auto s3 = __builtin_rvtt_sfpswap_indexed (v2, v0, c2, c0, 1);
  v2 = __builtin_rvtt_sfpselect4 (s3, 0);
  auto s4 = __builtin_rvtt_sfpswap_indexed (v3, v1, c3, c1, 1);
  v3 = __builtin_rvtt_sfpselect4 (s4, 0);
  v0 = __builtin_rvtt_sfpload (nullptr, 96, 0, 0, 0, 7);
  v1 = __builtin_rvtt_sfpload (nullptr, 100, 0, 0, 0, 7);
  c0 = __builtin_rvtt_sfpload (nullptr, 104, 0, 0, 0, 7);
  c1 = __builtin_rvtt_sfpload (nullptr, 108, 0, 0, 0, 7);
  c2 = __builtin_rvtt_sfpselect4 (s3, 2);
  c3 = __builtin_rvtt_sfpselect4 (s4, 2);
  auto t = __builtin_rvtt_sfptransp8 (v0, v1, v2, v3, c0, c1, c2, c3);
  v0 = __builtin_rvtt_sfpselect4 (t, 0);
  v1 = __builtin_rvtt_sfpselect4 (t, 1);
  v2 = __builtin_rvtt_sfpselect4 (t, 2);
  v3 = __builtin_rvtt_sfpselect4 (t, 3);
  __builtin_rvtt_sfpstore (nullptr, v0, 112, 0, 0, 0, 7);
  __builtin_rvtt_sfpstore (nullptr, v1, 116, 0, 0, 0, 7);
  __builtin_rvtt_sfpstore (nullptr, v2, 120, 0, 0, 0, 7);
  __builtin_rvtt_sfpstore (nullptr, v3, 124, 0, 0, 0, 7);
}
