// Check that dual-bank binding does not engage when every pin site has a single alternative.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-lreg-alloc -fdump-rtl-rvtt_lp_alloc-details" }
// { dg-final { scan-rtl-dump-not "dual-bank" "rvtt_lp_alloc" } }
// { dg-final { scan-rtl-dump "no-op \\(allocation left to IRA as today\\)" "rvtt_lp_alloc" } }

void dualbank_noop (void)
{
  /* Establish both lane facts; ENCC alone does not clear ROW_MASK.  */
  __builtin_rvtt_sfpencc (10, 3);
  __builtin_rvtt_sfpconfig_i (0, 15, 1);
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
