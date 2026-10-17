// Check that a rotate chain with no dominating all-lanes SFPENCC keeps every word
// (crosslane-lane-state-unproven).
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-crosslane -mno-tt-tensix-optimize-replay -fdump-tree-rvtt_crosslane" }

#define ROR1(x) __builtin_rvtt_sfpshft2_subvec_shfl1 (x, 3)

void unproven_turn ()
{
  auto v = __builtin_rvtt_sfpreadlreg (0);
  auto r = ROR1 (ROR1 (ROR1 (ROR1 (ROR1 (ROR1 (ROR1 (ROR1 (v))))))));
  __builtin_rvtt_sfpwritelreg (r, 1);
}

// { dg-final { scan-tree-dump "crosslane-lane-state-unproven" "rvtt_crosslane" } }
// { dg-final { scan-tree-dump-not "rotate chain collapse" "rvtt_crosslane" } }
// { dg-final { scan-assembler-times {SFPSHFT2\tL[0-9]+, L[0-9]+, 0, 3} 8 } }
