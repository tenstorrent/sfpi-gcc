// Check that without -mtt-tensix-optimize-crosslane the full rotate chains are emitted.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mno-tt-tensix-optimize-replay" }

#define ROR1(x) __builtin_rvtt_sfpshft2_subvec_shfl1 (x, 3)

void full_turn ()
{
  __builtin_rvtt_sfpencc_all_lanes ();
  __builtin_rvtt_sfpconfig_i (0, 15, 1);
  auto v = __builtin_rvtt_sfpreadlreg (0);
  auto r = ROR1 (ROR1 (ROR1 (ROR1 (ROR1 (ROR1 (ROR1 (ROR1 (v))))))));
  __builtin_rvtt_sfpwritelreg (r, 1);
}

// { dg-final { scan-assembler-times {SFPSHFT2\tL[0-9]+, L[0-9]+, 0, 3} 8 } }
