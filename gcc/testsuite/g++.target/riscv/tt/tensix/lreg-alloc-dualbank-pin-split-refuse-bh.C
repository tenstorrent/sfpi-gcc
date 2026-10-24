// Check that dual-bank binding refuses order-crossing pins that put two colors on one web.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-lreg-alloc -fdump-rtl-rvtt_lp_alloc-details" }
// { dg-final { scan-rtl-dump "dualbank-pin-inconsistent" "rvtt_lp_alloc" } }
// { dg-final { scan-rtl-dump-not "rewritten to hard LREGs" "rvtt_lp_alloc" } }

void dualbank_pin_split (void)
{
  /* Establish both lane facts; ENCC alone does not clear ROW_MASK.  */
  __builtin_rvtt_sfpencc (10, 3);
  __builtin_rvtt_sfpconfig_i (0, 15, 1);
  auto v0 = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
  auto v1 = __builtin_rvtt_sfpload (nullptr, 4, 0, 0, 0, 7);
  auto c0 = __builtin_rvtt_sfpload (nullptr, 8, 0, 0, 0, 7);
  auto c1 = __builtin_rvtt_sfpload (nullptr, 12, 0, 0, 0, 7);
  /* The first swap pins v0's web as a first value operand...  */
  auto s1 = __builtin_rvtt_sfpswap_indexed (v0, v1, c0, c1, 1);
  auto w0 = __builtin_rvtt_sfpselect4 (s1, 0);
  auto w1 = __builtin_rvtt_sfpselect4 (s1, 1);
  auto d0 = __builtin_rvtt_sfpselect4 (s1, 2);
  auto d1 = __builtin_rvtt_sfpselect4 (s1, 3);
  /* ...then transp8 pins w0 as operand 1 (L1) and w1 as operand 0 (L0):
     order-crossing single-color demands that cannot all be met when the
     swap ties w0 to v0's web (matching in/out).  */
  auto t = __builtin_rvtt_sfptransp8 (w1, w0, d0, d1, w0, w1, d1, d0);
  __builtin_rvtt_sfpstore (nullptr, __builtin_rvtt_sfpselect4 (t, 0), 16, 0, 0, 0, 7);
}
