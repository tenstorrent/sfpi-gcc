// MVE realization lockstep belt red/green (item #5 stage 2): the
// testing knob deliberately mis-rotates one committed rotation web
// (the copy's WRITER is re-pointed back at its old register while the
// readers keep the new one -- the classic partial-web wrong code).
// The producer-lockstep belt must catch the diverging value web
// (some reader's nearest-preceding producer changes identity), refuse
// by name, undo every rotation rename exactly, and leave the greedy
// pairing to commit byte-identically to the un-sabotaged
// no-realization stream (II 36 -> 29, the blockfree twin's result).
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap -mtt-tensix-optimize-crossrow-pairing -mtt-tensix-optimize-rename-temporal -mtt-tensix-optimize-mve-expand -mtt-tensix-mve-expand-sabotage -fdump-rtl-rvtt_schedule-details" }
// { dg-final { scan-rtl-dump "Crossrow mve-expand: undid 2 rotation rename\\(s\\) in bb \\d+" "rvtt_schedule" } }
// { dg-final { scan-rtl-dump "Crossrow mve-expand refused: mve-expand-lockstep-divergence in bb \\d+ \\(renames undone\\)" "rvtt_schedule" } }
// { dg-final { scan-rtl-dump-not "mve-expand committed" "rvtt_schedule" } }
// { dg-final { scan-rtl-dump "Crossrow pairing: bb \\d+ rows=2 nodes=22 II 36 -> 29" "rvtt_schedule" } }

void mve_sabotage_row ()
{
  for (unsigned r = 0; r != 32; ++r)
    {
      auto x = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      auto v1 = __builtin_rvtt_sfpmul (x, x, 0);
      auto v2 = __builtin_rvtt_sfpmad (x, x, x, 0);
      auto c1 = __builtin_rvtt_sfpmul (x, x, 1);
      auto c2 = __builtin_rvtt_sfpmad (c1, x, x, 0);
      auto c3 = __builtin_rvtt_sfpmad (c2, x, x, 0);
      auto c4 = __builtin_rvtt_sfpmad (c3, c2, x, 0);
      auto c5 = __builtin_rvtt_sfpmad (c4, x, x, 0);
      auto c6 = __builtin_rvtt_sfpmad (c5, c4, x, 0);
      auto res = __builtin_rvtt_sfpmad (c6, v1, v2, 0);
      __builtin_rvtt_sfpstore (nullptr, res, 0, 0, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
