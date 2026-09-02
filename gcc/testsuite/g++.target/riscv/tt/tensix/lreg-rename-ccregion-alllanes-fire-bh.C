// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-unroll-loops -mno-tt-tensix-optimize-replay -mtt-tensix-optimize-lreg-rename-chains -mtt-tensix-optimize-rename-cc-region -fdump-rtl-rvtt_lreg_rename_chains-details" }
// The ALL-LANES-ENTRY fire (the cc-span RTL-view widening): a
// storage-collision chain span crosses a canonical predicated region
// (the rvtt_cc ambient form: a depth-0 SETCC refinement restored by
// the word-exact all-lanes SFPENCC).  The blanket rule refuses this
// span (regrename-cc-span); the RTL view proves the span entry
// all-lanes by the kill-modeling backward walk (loop latch: the
// block's last CC event is the all-lanes ENCC; preheader: the
// function-entry ambient axiom), the end state restored word-exactly,
// and the rename fires.
// { dg-final { scan-rtl-dump "cc-span rtl-view: all-lanes-entry" "rvtt_lreg_rename_chains" } }
// { dg-final { scan-rtl-dump "Lreg chain rename: L\\d+ -> L\\d+" "rvtt_lreg_rename_chains" } }
// { dg-final { scan-rtl-dump-not "regrename-cc-span" "rvtt_lreg_rename_chains" } }
// { dg-final { scan-rtl-dump-not "regrename-postcommit-divergence" "rvtt_lreg_rename_chains" } }
void renc_ccr_alllanes ()
{
  auto k1 = __builtin_rvtt_sfpreadlreg (0);
  auto k2 = __builtin_rvtt_sfpreadlreg (1);
  auto x = __builtin_rvtt_sfpreadlreg (2);
  for (unsigned row = 0; row != 20; ++row)
    {
      auto t = __builtin_rvtt_sfpmul (k1, k2, 0);
      auto p = __builtin_rvtt_sfpmul (x, x, 0);
      /* Canonical predicated region between the colliding writers and
	 their consumers: at RTL this is [SETCC @ depth 0, ...,
	 all-lanes SFPENCC].  */
      {
	auto xg = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);
	__builtin_rvtt_sfppushc (0);
	__builtin_rvtt_sfpsetcc_v (xg, 0);
	auto zg = __builtin_rvtt_sfpassign_lv (xg, xg);
	__builtin_rvtt_sfpstore (nullptr, zg, 0, 0, 0, 6, 7);
	__builtin_rvtt_sfppopc (0);
      }
      auto r = __builtin_rvtt_sfpxor (p, t);
      auto u = __builtin_rvtt_sfpmul (k2, k1, 0);
      x = __builtin_rvtt_sfpxor (r, u);
    }
  __builtin_rvtt_sfpwritelreg (x, 2);
}
