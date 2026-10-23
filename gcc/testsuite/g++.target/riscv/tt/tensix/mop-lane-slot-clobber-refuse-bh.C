// Valid initialization does not survive a later unsafe payload overwrite.
// Template1 outer=inner=1 executes Loop0Last, slot7. Its REPLAY payload
// can swallow the subsequent flags reset; an arbitrary word can do so too.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-crosslane -mtt-tensix-optimize-prgm-const -mno-tt-tensix-optimize-replay -fdump-tree-rvtt_crosslane -fdump-rtl-rvtt_lreg_livein" }
// { dg-final { scan-tree-dump-not "rotate chain collapse" "rvtt_crosslane" } }
// { dg-final { scan-tree-dump-times "crosslane-lane-state-unproven" 2 "rvtt_crosslane" } }
// { dg-final { scan-assembler-times {SFPSHFT2\tL[0-9]+, L[0-9]+, 0, 3} 16 } }
// { dg-final { scan-rtl-dump-times {Lane merge: insn [0-9]+ keeps the old value of LREG mask 0x02} 2 "rvtt_lreg_livein" } }

#define RAW(W) asm volatile (".ttinsn %0" : : "n" (W))
#define SLOT(N, V) (((volatile unsigned *) 0xffb80000u)[N] = (V))
#define PROGRAM() do { SLOT (0, 1); SLOT (1, 1); \
  SLOT (2, 0x02000000u); SLOT (3, 0x02000000u); \
  SLOT (4, 0x02000000u); SLOT (5, 0x02000000u); \
  SLOT (6, 0x02000000u); SLOT (7, 0x02000000u); \
  SLOT (8, 0x02000000u); } while (0)
#define CHECK() do { \
  __builtin_rvtt_sfpencc_all_lanes (); \
  __builtin_rvtt_sfpconfig_i (0, 15, 1); \
  auto v = __builtin_rvtt_sfpreadlreg (0); \
  v = __builtin_rvtt_sfpshft2_subvec_shfl1 (v, 3); \
  v = __builtin_rvtt_sfpshft2_subvec_shfl1 (v, 3); \
  v = __builtin_rvtt_sfpshft2_subvec_shfl1 (v, 3); \
  v = __builtin_rvtt_sfpshft2_subvec_shfl1 (v, 3); \
  v = __builtin_rvtt_sfpshft2_subvec_shfl1 (v, 3); \
  v = __builtin_rvtt_sfpshft2_subvec_shfl1 (v, 3); \
  v = __builtin_rvtt_sfpshft2_subvec_shfl1 (v, 3); \
  v = __builtin_rvtt_sfpshft2_subvec_shfl1 (v, 3); \
  __builtin_rvtt_sfpwritelreg (v, 3); \
  RAW (0x71120001); __builtin_rvtt_sfprawlreg_effect (0, 2); \
  RAW (0x71120002); __builtin_rvtt_sfprawlreg_effect (0, 2); \
  __builtin_rvtt_sfpencc_all_lanes (); \
  __builtin_rvtt_sfpwritelreg (__builtin_rvtt_sfpreadlreg (1), 4); \
} while (0)

void overwritten_by_recorder ()
{
  PROGRAM ();
  SLOT (7, 0x04000011u);
  RAW (0x01800000);
  CHECK ();
}

void overwritten_by_unknown (unsigned word)
{
  PROGRAM ();
  SLOT (7, word);
  RAW (0x01800000);
  CHECK ();
}
