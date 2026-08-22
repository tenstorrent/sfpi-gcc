// Named-refusal twin: order-crossing single-alternative pins put
// two colors on one web (the transp8 operand order deliberately
// crosses the swap's matching-tied outputs).  The binding layer must
// refuse BY NAME and stand down -- the function still compiles
// exactly as today (LRA has repair headroom below peak pressure).
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-lreg-alloc -fdump-rtl-rvtt_lp_alloc-details" }
// { dg-final { scan-rtl-dump "dualbank-pin-inconsistent" "rvtt_lp_alloc" } }
// { dg-final { scan-rtl-dump-not "rewritten to hard LREGs" "rvtt_lp_alloc" } }

void dualbank_pin_split (void)
{
  auto v0 = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
  auto v1 = __builtin_rvtt_sfpload (nullptr, 4, 0, 0, 0, 7);
  auto c0 = __builtin_rvtt_sfpload (nullptr, 8, 0, 0, 0, 7);
  auto c1 = __builtin_rvtt_sfpload (nullptr, 12, 0, 0, 0, 7);
  /* First swap pins v0's web as a FIRST value operand...  */
  auto s1 = __builtin_rvtt_sfpswap_indexed (v0, v1, c0, c1, 1);
  auto w0 = __builtin_rvtt_sfpselect4 (s1, 0);
  auto w1 = __builtin_rvtt_sfpselect4 (s1, 1);
  auto d0 = __builtin_rvtt_sfpselect4 (s1, 2);
  auto d1 = __builtin_rvtt_sfpselect4 (s1, 3);
  /* ...then transp8 pins w0 as operand 1 (L1) while ALSO pinning w1 as
     operand 0 (L0): the same webs carry order-crossing single-color
     demands the whole-web model cannot satisfy in every combination
     when the swap ties w0==v0-web (matching in/out).  */
  auto t = __builtin_rvtt_sfptransp8 (w1, w0, d0, d1, w0, w1, d1, d0);
  __builtin_rvtt_sfpstore (nullptr, __builtin_rvtt_sfpselect4 (t, 0), 16, 0, 0, 0, 7);
}
