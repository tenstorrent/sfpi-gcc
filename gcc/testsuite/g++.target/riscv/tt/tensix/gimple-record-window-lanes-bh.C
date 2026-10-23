// A swallowed ENCC is not a physical reset.  The completed neighboring
// window, followed by an executed reset, must still admit the optimization.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-crosslane -mtt-tensix-optimize-opaque-replay-record -mno-tt-tensix-optimize-replay -fdump-tree-rvtt_crosslane" }

#define RAW(word) asm volatile (".ttinsn %0" : : "n" (word))
#define TURN() do { auto v = __builtin_rvtt_sfpreadlreg (0); \
  v = __builtin_rvtt_sfpshft2_subvec_shfl1 (v, 3); \
  v = __builtin_rvtt_sfpshft2_subvec_shfl1 (v, 3); \
  v = __builtin_rvtt_sfpshft2_subvec_shfl1 (v, 3); \
  v = __builtin_rvtt_sfpshft2_subvec_shfl1 (v, 3); \
  v = __builtin_rvtt_sfpshft2_subvec_shfl1 (v, 3); \
  v = __builtin_rvtt_sfpshft2_subvec_shfl1 (v, 3); \
  v = __builtin_rvtt_sfpshft2_subvec_shfl1 (v, 3); \
  v = __builtin_rvtt_sfpshft2_subvec_shfl1 (v, 3); \
  __builtin_rvtt_sfpwritelreg (v, 1); } while (0)

void captured_reset ()
{
  RAW (0x04000011); // one word, capture without execution
  RAW (0x8a00300a); // swallowed SFPENCC
  __builtin_rvtt_sfpconfig_i (0, 15, 1);
  TURN ();
}

void completed_window_reset ()
{
  RAW (0x04000011);
  RAW (0x00000000);
  __builtin_rvtt_sfpencc_all_lanes ();
  __builtin_rvtt_sfpconfig_i (0, 15, 1);
  TURN ();
}

// { dg-final { scan-tree-dump-times "rotate chain collapse 8 -> 0 links" 1 "rvtt_crosslane" } }
// { dg-final { scan-tree-dump-times "crosslane-lane-state-unproven" 1 "rvtt_crosslane" } }
// { dg-final { scan-assembler-times {SFPSHFT2\tL[0-9]+, L[0-9]+, 0, 3} 8 } }
