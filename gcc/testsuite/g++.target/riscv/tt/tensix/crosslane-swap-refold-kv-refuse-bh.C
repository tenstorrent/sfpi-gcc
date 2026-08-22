// Key-value refold refusal (crosslane-kv-refold-tie-unadjudicated):
// under ENABLE_DEST_INDEX the swap decision moves companion payloads,
// and the equal-key decision is an UNADJUDICATED doc-vs-sim divergence
// (lane FB finding: SFPSWAP.md keys tie swaps on sign; the pinned sim
// compares min c<d / max c>=d) -- a second identical exchange moves
// equal-key companions again under the doc model, so indexed refolding
// refuses by name until silicon adjudicates.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-crosslane -mno-tt-tensix-optimize-replay -fdump-tree-rvtt_crosslane" }

void kv_sorted_twice ()
{
  auto k0 = __builtin_rvtt_sfpreadlreg (0);
  auto k1 = __builtin_rvtt_sfpreadlreg (1);
  auto p0 = __builtin_rvtt_sfpreadlreg (4);
  auto p1 = __builtin_rvtt_sfpreadlreg (5);
  auto r1 = __builtin_rvtt_sfpswap_indexed (k0, k1, p0, p1, 1);
  auto ka = __builtin_rvtt_sfpselect4 (r1, 0);
  auto kb = __builtin_rvtt_sfpselect4 (r1, 1);
  auto pa = __builtin_rvtt_sfpselect4 (r1, 2);
  auto pb = __builtin_rvtt_sfpselect4 (r1, 3);
  auto r2 = __builtin_rvtt_sfpswap_indexed (ka, kb, pa, pb, 1);
  __builtin_rvtt_sfpwritelreg (__builtin_rvtt_sfpselect4 (r2, 0), 0);
  __builtin_rvtt_sfpwritelreg (__builtin_rvtt_sfpselect4 (r2, 1), 1);
  __builtin_rvtt_sfpwritelreg (__builtin_rvtt_sfpselect4 (r2, 2), 4);
  __builtin_rvtt_sfpwritelreg (__builtin_rvtt_sfpselect4 (r2, 3), 5);
}

// { dg-final { scan-tree-dump "crosslane-kv-refold-tie-unadjudicated" "rvtt_crosslane" } }
// { dg-final { scan-tree-dump-not "swap idempotence refold" "rvtt_crosslane" } }
// { dg-final { scan-assembler-times {SFPSWAP} 2 } }
