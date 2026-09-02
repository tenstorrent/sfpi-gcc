// Recognition-only proof for the fused-MAD admission (widened
// correction record): an UNFUSED mul+add chain is NEVER converted to a
// MAD by this pass -- MUL+ADD carries two roundings, SFPMAD one, so
// the conversion is bit-changing and banned.  Here the chain's
// constant is shared by two statements (not in any admitted class), so
// the pass leaves the function completely untouched: no candidate, no
// allocation, no SFPCONFIG, and -- decisively -- no sfpmad anywhere in
// the pass's gimple output.  The SFPMADs in the final assembly are the
// pre-existing downstream combine's, identical with the flag off
// (flag-off/flag-on byte identity is the pass-wide refusal invariant,
// gated by the corpus OFF leg).
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-prgm-const -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-not "prgm-const: allocated" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "sfpmad" "rvtt_prgm_const" } }
// { dg-final { scan-assembler-not "SFPCONFIG" } }

void muladd_not_converted ()
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  auto y = __builtin_rvtt_sfpreadlreg (1);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto k = __builtin_rvtt_sfpxloadi (nullptr, 0x3fb8aa3b, 0, 0, 31);
      auto px = __builtin_rvtt_sfpmul (x, k, 0);
      auto py = __builtin_rvtt_sfpmul (y, k, 0);
      auto b = __builtin_rvtt_sfpxloadi (nullptr, 0x3f317218, 0, 0, 31);
      x = __builtin_rvtt_sfpadd (px, b, 0);
      y = __builtin_rvtt_sfpadd (py, b, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
  __builtin_rvtt_sfpwritelreg (y, 1);
}
