// A genuinely private writer may use its enumerated safe caller binding
// in the template audit.  That does not supply the separate context-free
// PRGM/no-recorder proof needed to establish physical reset delivery.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-crosscall-hoist -fdump-tree-rvtt_crosscall" }
// { dg-final { scan-tree-dump "TU template audit: proven loadi-dests=0x8" "rvtt_crosscall" } }
// { dg-final { scan-tree-dump "crosscall-caller-row-mask-unproved" "rvtt_crosscall" } }
// { dg-final { scan-tree-dump-not "hoisted 6 contract materializations" "rvtt_crosscall" } }
// { dg-final { scan-tree-dump-not "mop-template-alias-unproven" "rvtt_crosscall" } }

#include "crosscall-hoist-scan-entry-offcontract-bh.C"

static volatile unsigned ordinary_data;
static __attribute__((noinline, noclone, noipa)) void private_pointer_writer (volatile unsigned *p,
							     unsigned word)
{
  *p = word;
}

static __attribute__((constructor)) void bind_private_writer ()
{
  private_pointer_writer (&ordinary_data, 7);
}
