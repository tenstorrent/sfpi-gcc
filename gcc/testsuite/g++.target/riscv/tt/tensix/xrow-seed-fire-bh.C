// Rule-B FULL-LANE-ROOT fire (the roundingops mechanism): the store's
// value web roots INSIDE a CC atom at a bare all-lanes copy (the
// SFPMOV mod-2 that provides the lv base for the predicated merge).
// The plain rename discipline refuses it (rename-cc-domain) and Rule-A
// pairing cannot improve the modeled II at all; under the seed
// sub-flag the full-lane root renames SEED-FREE (it writes every lane
// itself, so no disabled-lane bit can escape), both rows' webs
// decouple, and the modeled steady-state II drops strictly.  No
// function or opcode identity participates.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap -mtt-tensix-optimize-crossrow-pairing -mtt-tensix-optimize-crossrow-pairing-seed -fdump-rtl-rvtt_schedule-details" }
// { dg-final { scan-rtl-dump {Crossrow pairing seed: reg \d+ -> \d+ web at uid=\d+ \(\d+ insns\) seed none-full-lane-root II \d+ -> \d+} "rvtt_schedule" } }
// { dg-final { scan-rtl-dump {Crossrow pairing: bb \d+ rows=2 nodes=\d+ II \d+ -> \d+ renames=\d+ seeds=0} "rvtt_schedule" } }
// { dg-final { scan-rtl-dump "trips=32->16" "rvtt_schedule" } }
// { dg-final { scan-assembler {SFPMOV\tL[0-7], L[0-7], 2} } }
// { dg-final { scan-assembler {SFPLOAD\tL[0-7], 2, 0, 7} } }
// { dg-final { scan-assembler-times "TTINCRWC\t0, 4, 0, 0" 1 } }
// { dg-final { scan-assembler-not "TTINCRWC\t0, 2, 0, 0" } }

void full_lane_root_row ()
{
  for (unsigned r = 0; r != 32; ++r)
    {
      auto x = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      auto e = __builtin_rvtt_sfpexexp (x, 0);
      auto b = __builtin_rvtt_sfpmul (x, x, 0);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc_v (e, 0);
      auto c = __builtin_rvtt_sfpassign_lv (x, x);
      c = __builtin_rvtt_sfpassign_lv (c, b);
      __builtin_rvtt_sfppopc (0);
      c = __builtin_rvtt_sfpmad (c, x, x, 0);
      c = __builtin_rvtt_sfpmad (c, b, x, 0);
      c = __builtin_rvtt_sfpmad (c, x, x, 0);
      __builtin_rvtt_sfpstore (nullptr, c, 0, 0, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
