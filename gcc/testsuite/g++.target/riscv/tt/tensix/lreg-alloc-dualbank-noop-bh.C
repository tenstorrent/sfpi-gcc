// Engagement-gate twin: a function whose only pin sites are
// single-alternative (the SFPTRANSP8 quartet -- per-operand visible
// to IRA's cost model already) must NOT engage the dual-bank binding
// layer: no binding dump line, and the emitted bytes are identical to
// the flag-on compilation before the layer existed (the corpus gates
// carry the byte proof; this twin locks the structural gate).
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-lreg-alloc -fdump-rtl-rvtt_lp_alloc-details" }
// { dg-final { scan-rtl-dump-not "dual-bank" "rvtt_lp_alloc" } }
// { dg-final { scan-rtl-dump "no-op \\(allocation left to IRA as today\\)" "rvtt_lp_alloc" } }

void dualbank_noop (void)
{
  auto v0 = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
  auto v1 = __builtin_rvtt_sfpload (nullptr, 4, 0, 0, 0, 7);
  auto v2 = __builtin_rvtt_sfpload (nullptr, 8, 0, 0, 0, 7);
  auto v3 = __builtin_rvtt_sfpload (nullptr, 12, 0, 0, 0, 7);
  auto c0 = __builtin_rvtt_sfpload (nullptr, 16, 0, 0, 0, 7);
  auto c1 = __builtin_rvtt_sfpload (nullptr, 20, 0, 0, 0, 7);
  auto c2 = __builtin_rvtt_sfpload (nullptr, 24, 0, 0, 0, 7);
  auto c3 = __builtin_rvtt_sfpload (nullptr, 28, 0, 0, 0, 7);
  auto t = __builtin_rvtt_sfptransp8 (v0, v1, v2, v3, c0, c1, c2, c3);
  __builtin_rvtt_sfpstore (nullptr, __builtin_rvtt_sfpselect4 (t, 0), 32, 0, 0, 0, 7);
  __builtin_rvtt_sfpstore (nullptr, __builtin_rvtt_sfpselect4 (t, 1), 36, 0, 0, 0, 7);
}
