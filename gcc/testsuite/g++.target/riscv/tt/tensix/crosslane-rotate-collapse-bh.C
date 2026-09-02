// Cross-lane rotate-chain collapse (X4): ror1^n == ror1^(n mod
// 8) under a proven all-lanes state (SFPSHFT2.md; FB battery rotate
// inverses).  Renamed-equivalent + varied chain lengths and sources.
// Replay formation disabled only to keep the assembler counts literal.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-crosslane -mno-tt-tensix-optimize-replay -fdump-tree-rvtt_crosslane" }

#define ROR1(x) __builtin_rvtt_sfpshft2_subvec_shfl1 (x, 3)

// A full turn is the identity: the whole chain dissolves.
void full_turn ()
{
  __builtin_rvtt_sfpencc_all_lanes ();
  auto v = __builtin_rvtt_sfpreadlreg (0);
  auto r = ROR1 (ROR1 (ROR1 (ROR1 (ROR1 (ROR1 (ROR1 (ROR1 (v))))))));
  __builtin_rvtt_sfpwritelreg (r, 1);
}

// Renamed-equivalent with a varied length and source: 12 = 8 + 4.
void spun_thrice_plus_four ()
{
  __builtin_rvtt_sfpencc_all_lanes ();
  auto seed = __builtin_rvtt_sfploadi (nullptr, 0x41c8, 0, 0, 0);
  auto q1 = ROR1 (ROR1 (ROR1 (ROR1 (seed))));
  auto q2 = ROR1 (ROR1 (ROR1 (ROR1 (q1))));
  auto q3 = ROR1 (ROR1 (ROR1 (ROR1 (q2))));
  __builtin_rvtt_sfpwritelreg (q3, 2);
}

// Near miss: a 5-chain has nothing to collapse (5 mod 8 == 5) and must
// stay byte-identical -- no dump line, all five words.
void five_stays ()
{
  __builtin_rvtt_sfpencc_all_lanes ();
  auto w = __builtin_rvtt_sfpreadlreg (2);
  auto s = ROR1 (ROR1 (ROR1 (ROR1 (ROR1 (w)))));
  __builtin_rvtt_sfpwritelreg (s, 3);
}

// { dg-final { scan-tree-dump-times "rotate chain collapse 8 -> 0 links" 1 "rvtt_crosslane" } }
// { dg-final { scan-tree-dump-times "rotate chain collapse 12 -> 4 links" 1 "rvtt_crosslane" } }
// full_turn: zero shuffles; spun: exactly 4; five_stays: exactly 5.
// { dg-final { scan-assembler-times {SFPSHFT2\tL[0-9]+, L[0-9]+, 0, 3} 9 } }
