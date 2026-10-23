// Physical current-CFG nine-slot initialization, not a TU payload permission.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-crosslane -mtt-tensix-optimize-prgm-const -mno-tt-tensix-optimize-replay -fdump-tree-rvtt_crosslane" }
// { dg-final { scan-tree-dump-times "rotate chain collapse 8 -> 0" 1 "rvtt_crosslane" } }
// { dg-final { scan-assembler-not SFPSHFT2 } }
void initialized_delivery_then_reset (unsigned select)
{
  volatile unsigned *slots = (volatile unsigned *)0xffb80000u;
  slots[0] = 1; slots[1] = 1;
  slots[2] = 0x02000000u; slots[3] = 0x02000000u;
  slots[4] = 0x02000000u; slots[5] = 0x02000000u;
  slots[6] = 0x02000000u; slots[7] = 0x02000000u;
  slots[8] = 0x02000000u;
  *(volatile unsigned *)0xffe40000u = select ? 0x01800000u : 0x02000000u;
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
