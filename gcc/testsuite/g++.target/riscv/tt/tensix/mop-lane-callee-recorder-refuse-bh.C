// An unknown callee expansion can start recording before the caller resets
// lane state. Template writes in a different public entry do not initialize
// this call path or prove physical delivery of that reset.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-crosslane -mtt-tensix-optimize-prgm-const -mno-tt-tensix-optimize-replay -fdump-tree-rvtt_crosslane" }
// { dg-final { scan-tree-dump-not "rotate chain collapse" "rvtt_crosslane" } }
// { dg-final { scan-tree-dump "crosslane-lane-state-unproven" "rvtt_crosslane" } }
// { dg-final { scan-assembler-times {SFPSHFT2\tL[0-9]+, L[0-9]+, 0, 3} 8 } }

void program_unused_nop_template ()
{
  volatile unsigned *slots = (volatile unsigned *) 0xffb80000u;
  slots[0] = 1;
  slots[1] = 1;
  slots[2] = 0x02000000u;
  slots[3] = 0x02000000u;
  slots[4] = 0x02000000u;
  slots[5] = 0x02000000u;
  slots[6] = 0x02000000u;
  slots[7] = 0x02000000u;
  slots[8] = 0x02000000u;
}

static __attribute__((noinline, noclone, noipa)) void unknown_callee_mop ()
{
  asm volatile (".ttinsn %0" : : "n" (0x01800000));
}

void reset_after_unknown_callee ()
{
  unknown_callee_mop ();
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
