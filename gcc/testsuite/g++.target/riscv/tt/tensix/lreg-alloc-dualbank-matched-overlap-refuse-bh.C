// Named-refusal twin: a swap input consumed again AFTER the swap
// stays live across the insn whose matching constraint must overwrite
// its register -- binding the pair would be a same-register overlap
// (silent wrong code), so the layer refuses BY NAME and leaves LRA's
// repair copy in charge, exactly as today.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-lreg-alloc -fdump-rtl-rvtt_lp_alloc-details" }
// { dg-final { scan-rtl-dump "dualbank-matched-webs-conflict" "rvtt_lp_alloc" } }
// { dg-final { scan-rtl-dump-not "rewritten to hard LREGs" "rvtt_lp_alloc" } }

void dualbank_matched_overlap (void)
{
  auto v0 = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
  auto v1 = __builtin_rvtt_sfpload (nullptr, 4, 0, 0, 0, 7);
  auto c0 = __builtin_rvtt_sfpload (nullptr, 8, 0, 0, 0, 7);
  auto c1 = __builtin_rvtt_sfpload (nullptr, 12, 0, 0, 0, 7);
  auto s1 = __builtin_rvtt_sfpswap_indexed (v0, v1, c0, c1, 1);
  auto w0 = __builtin_rvtt_sfpselect4 (s1, 0);
  /* v0 is USED AGAIN after the swap: its web stays live across the
     insn that must overwrite its register (matching in/out).  A
     repair copy is required -- this layer refuses and leaves the
     repair to LRA exactly as today.  */
  __builtin_rvtt_sfpstore (nullptr, v0, 16, 0, 0, 0, 7);
  __builtin_rvtt_sfpstore (nullptr, w0, 20, 0, 0, 0, 7);
}
