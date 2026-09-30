// CC-window refusal (crosslane-cc-window): a lane enabled only for the
// second exchange would really sort, so any CC-state change between
// the exchanges -- including an all-lanes re-enable -- refuses the
// refold.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-crosslane -mno-tt-tensix-optimize-replay -fdump-tree-rvtt_crosslane" }

void resorted_across_cc ()
{
  auto a = __builtin_rvtt_sfpreadlreg (0);
  auto b = __builtin_rvtt_sfpreadlreg (1);
  auto r1 = __builtin_rvtt_sfpswap (a, b, 1);
  auto a1 = __builtin_rvtt_sfpselect2 (r1, 0);
  auto b1 = __builtin_rvtt_sfpselect2 (r1, 1);
  __builtin_rvtt_sfpencc_all_lanes ();
  auto r2 = __builtin_rvtt_sfpswap (a1, b1, 1);
  __builtin_rvtt_sfpwritelreg (__builtin_rvtt_sfpselect2 (r2, 0), 2);
  __builtin_rvtt_sfpwritelreg (__builtin_rvtt_sfpselect2 (r2, 1), 3);
}

// { dg-final { scan-tree-dump "crosslane-cc-window" "rvtt_crosslane" } }
// { dg-final { scan-assembler-times {SFPSWAP} 2 } }
