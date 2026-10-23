// A field assignment is not a complete delivered word: lowering may merge
// unproved bits from the FIFO read before its actual word-sized write.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-crosslane -mtt-tensix-optimize-prgm-const -mno-tt-tensix-optimize-replay -fdump-tree-rvtt_crosslane" }
// { dg-final { scan-tree-dump-not "rotate chain collapse" "rvtt_crosslane" } }
// { dg-final { scan-tree-dump "crosslane-lane-state-unproven" "rvtt_crosslane" } }
// { dg-final { scan-assembler-times {SFPSHFT2\tL[0-9]+, L[0-9]+, 0, 3} 8 } }
struct fifo_word { unsigned bit : 1; unsigned remaining : 31; };
void partial_fifo_write_then_reset ()
{
  ((volatile fifo_word *)0xffe40000u)->bit = 1;
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
