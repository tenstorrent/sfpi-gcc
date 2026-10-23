// Check that dual-bank binding refuses a swap input that stays live across its matching-constraint overwrite.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-lreg-alloc -fdump-rtl-rvtt_lp_alloc-details" }
// { dg-final { scan-rtl-dump "dualbank-matched-webs-conflict" "rvtt_lp_alloc" } }
// { dg-final { scan-rtl-dump-not "rewritten to hard LREGs" "rvtt_lp_alloc" } }

void dualbank_matched_overlap (void)
{
  /* Establish both lane facts; ENCC alone does not clear ROW_MASK.  */
  __builtin_rvtt_sfpencc (10, 3);
  __builtin_rvtt_sfpconfig_i (0, 15, 1);
  auto v0 = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
  auto v1 = __builtin_rvtt_sfpload (nullptr, 4, 0, 0, 0, 7);
  auto c0 = __builtin_rvtt_sfpload (nullptr, 8, 0, 0, 0, 7);
  auto c1 = __builtin_rvtt_sfpload (nullptr, 12, 0, 0, 0, 7);
  auto s1 = __builtin_rvtt_sfpswap_indexed (v0, v1, c0, c1, 1);
  auto w0 = __builtin_rvtt_sfpselect4 (s1, 0);
  /* v0 is used again after the swap, so it stays live across the insn
     that must overwrite its register; LRA inserts the repair copy.  */
  __builtin_rvtt_sfpstore (nullptr, v0, 16, 0, 0, 0, 7);
  __builtin_rvtt_sfpstore (nullptr, w0, 20, 0, 0, 0, 7);
}
