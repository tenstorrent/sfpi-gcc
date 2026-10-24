// Unknown entry, SFPENCC alone and a nonzero ROW_MASK must all refuse the
// depth-zero CC span. These are not balanced-frame entry proofs.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-unroll-loops -mno-tt-tensix-optimize-replay -mtt-tensix-optimize-lreg-rename-chains -mtt-tensix-optimize-rename-cc-region -fdump-rtl-rvtt_lreg_rename_chains-details" }
// Each function has two candidates that reach the CC-span proof; both must
// refuse. A third candidate refuses its register constraint before that proof.
// Ordinary chains wholly outside the masked frame may still rename.
// { dg-final { scan-rtl-dump-times "cc-span rtl-view: refuse-entry-unproven" 6 "rvtt_lreg_rename_chains" } }
// { dg-final { scan-rtl-dump-times "Lreg chain rename refused: regrename-constraint" 3 "rvtt_lreg_rename_chains" } }
// { dg-final { scan-rtl-dump-not "regrename-postcommit-divergence" "rvtt_lreg_rename_chains" } }

__attribute__((always_inline)) static inline void
cc_span_body ()
{
  auto k1 = __builtin_rvtt_sfpreadlreg (0);
  auto k2 = __builtin_rvtt_sfpreadlreg (1);
  auto x = __builtin_rvtt_sfpreadlreg (2);
  for (unsigned row = 0; row != 20; ++row)
    {
      auto t = __builtin_rvtt_sfpmul (k1, k2, 0);
      auto p = __builtin_rvtt_sfpmul (x, x, 0);
      auto gate = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc (gate, 0);
      auto merged = __builtin_rvtt_sfpassign_lv (gate, gate);
      __builtin_rvtt_sfpstore (nullptr, merged, 0, 0, 0, 6, 7);
      __builtin_rvtt_sfppopc (0);
      auto result = __builtin_rvtt_sfpxor (p, t);
      auto u = __builtin_rvtt_sfpmul (k2, k1, 0);
      x = __builtin_rvtt_sfpxor (result, u);
    }
  __builtin_rvtt_sfpwritelreg (x, 2);
}

void unknown_entry ()
{
  cc_span_body ();
}

void flags_only ()
{
  __builtin_rvtt_sfpencc_all_lanes ();
  cc_span_body ();
}

void row_mask_not_zero ()
{
  __builtin_rvtt_sfpencc_all_lanes ();
  __builtin_rvtt_sfpconfig_i (1, 15, 1);
  cc_span_body ();
}
