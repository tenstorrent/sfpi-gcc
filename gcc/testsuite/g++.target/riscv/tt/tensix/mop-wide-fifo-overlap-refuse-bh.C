// A wide store starting before the FIFO can deliver its high half to word zero.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-crosslane -mtt-tensix-optimize-prgm-const -mno-tt-tensix-optimize-replay -fdump-tree-rvtt_crosslane" }
// { dg-final { scan-tree-dump-not "rotate chain collapse" "rvtt_crosslane" } }
// { dg-final { scan-tree-dump "crosslane-lane-state-unproven" "rvtt_crosslane" } }
// { dg-final { scan-assembler-times {SFPSHFT2\tL[0-9]+, L[0-9]+, 0, 3} 8 } }
void wide_fifo_overlap_then_reset ()
{
  *(volatile unsigned long long *)0xffe3fffcu = 0x0180000000000000ull;
  __builtin_rvtt_sfpencc_all_lanes ();
  __builtin_rvtt_sfpconfig_i (0,15,1);
  auto v = __builtin_rvtt_sfpreadlreg (0);
  v = __builtin_rvtt_sfpshft2_subvec_shfl1 (v,3);
  v = __builtin_rvtt_sfpshft2_subvec_shfl1 (v,3);
  v = __builtin_rvtt_sfpshft2_subvec_shfl1 (v,3);
  v = __builtin_rvtt_sfpshft2_subvec_shfl1 (v,3);
  v = __builtin_rvtt_sfpshft2_subvec_shfl1 (v,3);
  v = __builtin_rvtt_sfpshft2_subvec_shfl1 (v,3);
  v = __builtin_rvtt_sfpshft2_subvec_shfl1 (v,3);
  v = __builtin_rvtt_sfpshft2_subvec_shfl1 (v,3);
  __builtin_rvtt_sfpwritelreg (v,1);
}
