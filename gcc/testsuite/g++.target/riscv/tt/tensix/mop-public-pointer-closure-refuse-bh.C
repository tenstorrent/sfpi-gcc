// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-crosslane -mtt-tensix-optimize-prgm-const -mno-tt-tensix-optimize-replay -fdump-tree-rvtt_crosslane" }
// { dg-final { scan-tree-dump-not "rotate chain collapse" "rvtt_crosslane" } }
// { dg-final { scan-tree-dump "crosslane-lane-state-unproven" "rvtt_crosslane" } }
// { dg-final { scan-assembler-times {SFPSHFT2\tL[0-9]+, L[0-9]+, 0, 3} 8 } }
// Read-only adversarial probe: an uninitialized MOP delivered by the
// audited scalar store/load/consume channel, not a .ttinsn directive.
void unused_public_programmer ()
{
  volatile unsigned *slots = (volatile unsigned *)0xffb80000u;
  slots[0] = 1; slots[1] = 1;
  slots[2] = 0x02000000u; slots[3] = 0x02000000u;
  slots[4] = 0x02000000u; slots[5] = 0x02000000u;
  slots[6] = 0x02000000u; slots[7] = 0x02000000u;
  slots[8] = 0x02000000u;
}
// Independently callable public ABI entry: zero TU callers is not DATA proof.
void unknown_public_delivery (volatile unsigned *fifo, unsigned word)
{
  asm volatile ("sw %0, (%1)\n\tlw %0, (%1)\n\tand x0, x0, %0"
                : "+r" (word) : "r" (fifo) : "memory");
}
void caller_reset_after_unknown_expansion ()
{
  asm volatile ("fence" : : : "memory");
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
