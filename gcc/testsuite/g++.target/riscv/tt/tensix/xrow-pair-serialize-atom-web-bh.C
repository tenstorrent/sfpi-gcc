// Serialization regression (the simulator-caught WAR defect): the store's
// value web roots INSIDE a CC atom, so it cannot rename
// (rename-cc-domain) and the copy's redefinition must stay behind the
// first row's store in the dependence-legal candidate.  The pairing
// still commits -- the early pure spans interleave -- and the doubled
// separator proves the commit; the unsound span construction that
// ordered the copy's atom ahead of the first store is the shape this
// twin pins against.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap -mtt-tensix-optimize-crossrow-pairing -fdump-rtl-rvtt_schedule-details" }
// { dg-final { scan-rtl-dump "crossrow-pairing-rename-cc-domain" "rvtt_schedule" } }
// { dg-final { scan-rtl-dump "Crossrow pairing: bb \\d+ rows=2" "rvtt_schedule" } }
// { dg-final { scan-assembler-times "TTINCRWC\t0, 4, 0, 0" 1 } }
// { dg-final { scan-assembler {SFPLOAD\tL[0-7], 2, 0, 7} } }

void atom_web_to_store_row ()
{
  for (unsigned r = 0; r != 32; ++r)
    {
      auto x = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      auto a = __builtin_rvtt_sfpabs (x, 1);
      auto b = __builtin_rvtt_sfpmul (a, x, 0);
      auto c = __builtin_rvtt_sfpmad (b, a, x, 0);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc_v (c, 0);
      c = __builtin_rvtt_sfpadd (c, x, 0);
      __builtin_rvtt_sfppopc (0);
      __builtin_rvtt_sfpstore (nullptr, c, 0, 0, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
