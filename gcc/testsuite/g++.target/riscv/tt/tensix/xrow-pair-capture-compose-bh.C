// Composition with the counted-loop replay capture: the paired row keeps
// the capturable shape (all words replay-safe, one trailing typed
// separator, canonical countdown), so the downstream capture still fires
// on the doubled payload -- delivery stays record-plus-launch with the
// launches halved.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap -mtt-tensix-optimize-crossrow-pairing -mtt-tensix-optimize-replay -mtt-tensix-optimize-replay-hoist -fdump-rtl-rvtt_schedule-details -fdump-rtl-rvtt_replay-details" }
// { dg-final { scan-rtl-dump "Crossrow pairing: bb \\d+ rows=2" "rvtt_schedule" } }
// { dg-final { scan-rtl-dump "Counted-loop replay payload bb \\d+ length 22" "rvtt_replay" } }
// { dg-final { scan-assembler "TTREPLAY" } }
// { dg-final { scan-assembler {SFPLOAD\tL[0-7], 2, 0, 7} } }
// { dg-final { scan-assembler "TTINCRWC\t0, 4, 0, 0" } }

void paired_captured_row ()
{
  for (unsigned r = 0; r != 32; ++r)
    {
      auto x = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      auto a = __builtin_rvtt_sfpabs (x, 1);
      auto b = __builtin_rvtt_sfpmul (a, x, 0);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc_v (b, 0);
      a = __builtin_rvtt_sfpadd (a, x, 0);
      __builtin_rvtt_sfppopc (0);
      b = __builtin_rvtt_sfpmad (a, b, x, 0);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc_v (b, 0);
      a = __builtin_rvtt_sfpxor (a, b);
      __builtin_rvtt_sfppopc (0);
      __builtin_rvtt_sfpstore (nullptr, a, 0, 0, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
