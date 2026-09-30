// Cross-lane swap refolding (X4): re-exchanging an exchanged
// pair in the same operand roles is the identity on the results, exact
// under BOTH tie models (SFPSWAP.md; FB battery swap table); a mod-0
// pair cancels to the original operands.  The obligation is only an
// unchanged-CC window -- min/max is per-lane idempotent whatever the
// enable state, so no all-lanes proof is required.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-crosslane -mno-tt-tensix-optimize-replay -fdump-tree-rvtt_crosslane" }

// Same-role repeat under the vector min/max mode.
void sorted_twice ()
{
  auto a = __builtin_rvtt_sfpreadlreg (0);
  auto b = __builtin_rvtt_sfpreadlreg (1);
  auto r1 = __builtin_rvtt_sfpswap (a, b, 1);
  auto a1 = __builtin_rvtt_sfpselect2 (r1, 0);
  auto b1 = __builtin_rvtt_sfpselect2 (r1, 1);
  auto r2 = __builtin_rvtt_sfpswap (a1, b1, 1);
  __builtin_rvtt_sfpwritelreg (__builtin_rvtt_sfpselect2 (r2, 0), 2);
  __builtin_rvtt_sfpwritelreg (__builtin_rvtt_sfpselect2 (r2, 1), 3);
}

// Renamed-equivalent with a varied row-group mode (Mod1 = 4).
void row_sorted_twice ()
{
  auto x = __builtin_rvtt_sfpreadlreg (2);
  auto y = __builtin_rvtt_sfpreadlreg (3);
  auto s1 = __builtin_rvtt_sfpswap (x, y, 4);
  auto x1 = __builtin_rvtt_sfpselect2 (s1, 0);
  auto y1 = __builtin_rvtt_sfpselect2 (s1, 1);
  auto s2 = __builtin_rvtt_sfpswap (x1, y1, 4);
  __builtin_rvtt_sfpwritelreg (__builtin_rvtt_sfpselect2 (s2, 0), 0);
  __builtin_rvtt_sfpwritelreg (__builtin_rvtt_sfpselect2 (s2, 1), 1);
}

// The unconditional (mod 0) pair cancels entirely: both swaps go.
void swapped_back ()
{
  auto p = __builtin_rvtt_sfpreadlreg (0);
  auto q = __builtin_rvtt_sfpreadlreg (1);
  auto t1 = __builtin_rvtt_sfpswap (p, q, 0);
  auto p1 = __builtin_rvtt_sfpselect2 (t1, 0);
  auto q1 = __builtin_rvtt_sfpselect2 (t1, 1);
  auto t2 = __builtin_rvtt_sfpswap (p1, q1, 0);
  __builtin_rvtt_sfpwritelreg (__builtin_rvtt_sfpselect2 (t2, 0), 2);
  __builtin_rvtt_sfpwritelreg (__builtin_rvtt_sfpselect2 (t2, 1), 3);
}

// Near miss: DIFFERENT mods never refold.
void mixed_modes_stay ()
{
  auto c = __builtin_rvtt_sfpreadlreg (2);
  auto d = __builtin_rvtt_sfpreadlreg (3);
  auto u1 = __builtin_rvtt_sfpswap (c, d, 1);
  auto c1 = __builtin_rvtt_sfpselect2 (u1, 0);
  auto d1 = __builtin_rvtt_sfpselect2 (u1, 1);
  auto u2 = __builtin_rvtt_sfpswap (c1, d1, 2);
  __builtin_rvtt_sfpwritelreg (__builtin_rvtt_sfpselect2 (u2, 0), 0);
  __builtin_rvtt_sfpwritelreg (__builtin_rvtt_sfpselect2 (u2, 1), 1);
}

// { dg-final { scan-tree-dump-times "swap idempotence refold" 2 "rvtt_crosslane" } }
// { dg-final { scan-tree-dump-times "swap-pair cancel" 1 "rvtt_crosslane" } }
// sorted_twice 1 + row_sorted_twice 1 + swapped_back 0 + mixed 2.
// { dg-final { scan-assembler-times {SFPSWAP} 4 } }
