// Default-off control for the rotate-chain collapse: without
// -mtt-tensix-optimize-crosslane the full chains are emitted.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mno-tt-tensix-optimize-replay" }

#define ROR1(x) __builtin_rvtt_sfpshft2_subvec_shfl1 (x, 3)

void full_turn ()
{
  __builtin_rvtt_sfpencc_all_lanes ();
  auto v = __builtin_rvtt_sfpreadlreg (0);
  auto r = ROR1 (ROR1 (ROR1 (ROR1 (ROR1 (ROR1 (ROR1 (ROR1 (v))))))));
  __builtin_rvtt_sfpwritelreg (r, 1);
}

// { dg-final { scan-assembler-times {SFPSHFT2\tL[0-9]+, L[0-9]+, 0, 3} 8 } }
