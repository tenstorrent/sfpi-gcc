// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-unroll-loops -mtt-tensix-optimize-invariant-loadi -mtt-tensix-optimize-crossloop-hoist -mtt-tensix-optimize-cc-region-general -fdump-tree-rvtt_crossloop" }
// { dg-final { scan-tree-dump-not "hoisted across loop" "rvtt_crossloop" } }
// { dg-final { scan-tree-dump "crossloop-cc-ambient-unproven" "rvtt_crossloop" } }
// { dg-final { scan-tree-dump-not "proven ALL-LANES" "rvtt_crossloop" } }
#include "crossloop-hoist-ccgeneral-allanes-fire-bh.C"

// A TU payload MAY audit cannot initialize the private callee's live template.
void independent_public_template_programmer ()
{
  volatile unsigned *slots = (volatile unsigned *)0xffb80000u;
  slots[0] = 1; slots[1] = 1;
  slots[2] = 0x02000000u; slots[3] = 0x02000000u;
  slots[4] = 0x02000000u; slots[5] = 0x02000000u;
  slots[6] = 0x02000000u; slots[7] = 0x02000000u;
  slots[8] = 0x02000000u;
}
static __attribute__((noinline,noclone,noipa)) void missing_template_launch ()
{
  asm volatile (".ttinsn %0" : : "n" (0x01800000u));
}
void independent_public_mop_entry ()
{
  missing_template_launch ();
}
