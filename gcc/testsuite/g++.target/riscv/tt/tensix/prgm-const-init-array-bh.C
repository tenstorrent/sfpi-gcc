// The crt0 init-array walk: an indirect call whose pointer derives
// only from __init_array_start is proven to reach exactly this TU's
// own registered static constructors (all scanned by the TU walk), so
// it no longer refuses the freedom proof.  The bss-clear and
// loader-copy loops through the C-runtime data anchors are proven
// FIFO-disjoint by the link-image fact.  Fire: the kernel body
// allocates.  A static constructor with a benign body is present so
// the walk really has a registered callee to cover.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-prgm-const -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump "init-array walk call in .* proven" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump "prgm-const: allocated PRGM L\\d+ for invariant immediate" "rvtt_prgm_const" } }
// { dg-final { scan-assembler "SFPMAD" } }

typedef unsigned int u32;
extern volatile char __ldm_bss_start[], __ldm_bss_end[];
extern void (*__init_array_start[]) (void);
extern void (*__init_array_end[]) (void);

struct configured_scale
{
  unsigned bits;
  configured_scale () { bits = 0x42fe0000; }
};
static configured_scale the_scale;

__attribute__ ((noinline)) void boot_walk ()
{
  for (volatile u32 *p = (volatile u32 *) __ldm_bss_start;
       p < (volatile u32 *) __ldm_bss_end; p++)
    *p = 0;
  for (void (**ctor) (void) = __init_array_start;
       ctor < __init_array_end; ctor++)
    (*ctor) ();
}

void kernel_body ()
{
  boot_walk ();
  auto x = __builtin_rvtt_sfpreadlreg (0);
  auto s = __builtin_rvtt_sfpreadlreg (1);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto prod = __builtin_rvtt_sfpmul (x, s, 0);
      x = __builtin_rvtt_sfpaddi (nullptr, prod, 0x42fe, 0, 0, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}
