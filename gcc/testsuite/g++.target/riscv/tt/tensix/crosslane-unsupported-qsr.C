// Per-arch capability refusal: Quasar has no pinned-simulator proof
// battery for the cross-lane algebra, so the whole pass refuses by
// name and the stream stays byte-identical.
// { dg-options "-mcpu=tt-qsr32-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-crosslane -mno-tt-tensix-optimize-replay -fdump-tree-rvtt_crosslane" }

#define ROR1(x) __builtin_rvtt_sfpshft2_subvec_shfl1 (x, 3)

void full_turn ()
{
  __builtin_rvtt_sfpencc_all_lanes ();
  auto v = __builtin_rvtt_sfpreadlreg (0);
  auto r = ROR1 (ROR1 (ROR1 (ROR1 (ROR1 (ROR1 (ROR1 (ROR1 (v))))))));
  __builtin_rvtt_sfpwritelreg (r, 1);
}

// { dg-final { scan-tree-dump "crosslane-unsupported-target" "rvtt_crosslane" } }
// { dg-final { scan-tree-dump-not "rotate chain collapse" "rvtt_crosslane" } }
// { dg-final { scan-assembler-times {SFPSHFT2} 8 } }
