// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-unroll-loops -mtt-tensix-optimize-invariant-loadi -mtt-tensix-optimize-crossloop-hoist -fdump-tree-rvtt_crossloop" }
// Fail-closed twin (from an adversarial audit):
// a static address-taken template programmer stores a PARAM-relative
// slot word (the ckernel_template::program shape -- a deferred slot
// DEMAND) and is called only through a function pointer, so it has
// ZERO enumerable cgraph caller edges.  The resolver's fail-closed
// contract ("every site of every demand must resolve") was vacuously
// satisfied: the demand was silently dropped and the earlier census
// reported "proven loadi-dests=0" while the pointed-to template object
// programs an SFPLOADI -> L3 -- the crossloop hoist then lifted L-file
// materializations across the MOP-carrying loop (fail-open fire,
// verified on the earlier installed binary).  A demand with no resolved
// site must refuse by name.
// { dg-final { scan-tree-dump "mop-template-slot-caller-unenumerable" "rvtt_crossloop" } }
// { dg-final { scan-tree-dump "refused .crossloop-mop-slot-unproven." "rvtt_crossloop" } }
// { dg-final { scan-tree-dump-not "hoisted across" "rvtt_crossloop" } }

typedef volatile unsigned int vu32;
extern volatile unsigned int __instrn_buffer[];

struct xdke_tmpl { unsigned w0, w1, w2, w3, w4, w5, w6, w7, w8; };

__attribute__((noinline)) void
xdke_kernel (int tiles)
{
  for (int t = 0; t != tiles; ++t)
    {
      __asm__ __volatile__ (".ttinsn %0" : : "n" (0x01800000));   // TTI_MOP
      __asm__ __volatile__ (".ttinsn %0" : : "n" (0x37000104));
      for (int row = 0; row != 8; ++row)
	{
	  auto c0 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b1a3d, 0, 0, -32);
	  auto c1 = __builtin_rvtt_sfpxloadi (nullptr, 0x3f2e8ba3, 0, 0, -32);
	  auto x = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);
	  x = __builtin_rvtt_sfpmad (x, c0, c1, 0);
	  __builtin_rvtt_sfpstore (nullptr, x, 0, 0, 0, 6, 7);
	  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
	}
    }
}

// The dead guarded call keeps the programmer AFTER the kernel in the
// expansion order, so the kernel-time census still sees its gimple
// (otherwise body-unavailability masks the demand machinery).
static void
xdke_program_template (const xdke_tmpl *p)
{
  ((vu32 *) 0xFFB80000)[0] = 4;
  ((vu32 *) 0xFFB80000)[1] = 2;
  ((vu32 *) 0xFFB80000)[2] = 0x02000000;    // Tensix NOP start op
  ((vu32 *) 0xFFB80000)[3] = 0x37020044;    // SETRWC end op
  ((vu32 *) 0xFFB80000)[4] = 0x02000000;
  ((vu32 *) 0xFFB80000)[5] = p->w5;         // param-relative slot DEMAND
  ((vu32 *) 0xFFB80000)[6] = 0x02000000;
  ((vu32 *) 0xFFB80000)[7] = 0x02000000;
  ((vu32 *) 0xFFB80000)[8] = 0x02000000;
  if (p->w0 == 0xdeadbeefu)
    xdke_kernel (1);
}

static const xdke_tmpl xdke_t = { 0, 0, 0, 0, 0,
				  0x71312345u,	// SFPLOADI -> L3
				  0, 0, 0 };
void (*volatile xdke_program) (const xdke_tmpl *) = xdke_program_template;

extern "C" void _start ()
{
  xdke_program (&xdke_t);
  xdke_kernel (4);
}
