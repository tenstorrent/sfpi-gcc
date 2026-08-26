// Composition with the counted-loop replay capture: the Rule-B-renamed
// doubled row (two full-lane roots renamed seed-free, full interleave)
// keeps the capturable shape, so the downstream capture still fires on
// the paired payload -- delivery stays record-plus-launch with the
// launches halved.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap -mtt-tensix-optimize-crossrow-pairing -mtt-tensix-optimize-crossrow-pairing-seed -mtt-tensix-optimize-replay -mtt-tensix-optimize-replay-hoist -fdump-rtl-rvtt_schedule-details -fdump-rtl-rvtt_replay-details" }
// { dg-final { scan-rtl-dump {Crossrow pairing seed: reg \d+ -> \d+ web at uid=\d+ \(\d+ insns\) seed none-full-lane-root II \d+ -> \d+} "rvtt_schedule" } }
// { dg-final { scan-rtl-dump {Crossrow pairing: bb \d+ rows=2 nodes=\d+ II \d+ -> \d+ renames=\d+ seeds=0} "rvtt_schedule" } }
// { dg-final { scan-rtl-dump "Counted-loop replay payload bb \\d+ length 22" "rvtt_replay" } }
// { dg-final { scan-assembler "TTREPLAY" } }
// { dg-final { scan-assembler "TTINCRWC\t0, 4, 0, 0" } }

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
