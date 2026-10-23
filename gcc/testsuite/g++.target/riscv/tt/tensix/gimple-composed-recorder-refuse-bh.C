// An opcode-looking constant is not a bound on an unmasked runtime field.
// 0xb2020000 + 0x52000001 wraps to a REPLAY recorder word 0x04020001.
// A later explicit reset therefore does not prove physical issuance.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-crosslane -mno-tt-tensix-optimize-replay -fdump-tree-rvtt_crosslane" }

void composed_recorder (unsigned operand)
{
  *reinterpret_cast<volatile unsigned *> (0xffe40000u)
    = 0xb2020000u + operand;
  __builtin_rvtt_sfpencc_all_lanes ();
  __builtin_rvtt_sfpconfig_i (0, 15, 1);
  auto v = __builtin_rvtt_sfpreadlreg (0);
  v = __builtin_rvtt_sfpshft2_subvec_shfl1 (v, 3);
  v = __builtin_rvtt_sfpshft2_subvec_shfl1 (v, 3);
  v = __builtin_rvtt_sfpshft2_subvec_shfl1 (v, 3);
  v = __builtin_rvtt_sfpshft2_subvec_shfl1 (v, 3);
  v = __builtin_rvtt_sfpshft2_subvec_shfl1 (v, 3);
  v = __builtin_rvtt_sfpshft2_subvec_shfl1 (v, 3);
  v = __builtin_rvtt_sfpshft2_subvec_shfl1 (v, 3);
  v = __builtin_rvtt_sfpshft2_subvec_shfl1 (v, 3);
  __builtin_rvtt_sfpwritelreg (v, 1);
}

// { dg-final { scan-tree-dump-not "rotate chain collapse" "rvtt_crosslane" } }
// { dg-final { scan-tree-dump "crosslane-lane-state-unproven" "rvtt_crosslane" } }
// { dg-final { scan-assembler-times {SFPSHFT2\tL[0-9]+, L[0-9]+, 0, 3} 8 } }
