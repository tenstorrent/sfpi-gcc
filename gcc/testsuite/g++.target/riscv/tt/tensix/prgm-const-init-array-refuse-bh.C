// Check that a section-extended .init_array and an unanchored indirect call refuse the TU freedom proof.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-prgm-const -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump "refused .opaque-region-undeclared.: indirect call" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "init-array walk call in .* proven" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "prgm-const: allocated" "rvtt_prgm_const" } }
// { dg-final { scan-assembler-not "SFPMAD" } }

extern void (*__init_array_start[]) (void);
extern void (*__init_array_end[]) (void);

// The section-attribute extension: this pointer lands in .init_array
// at link time without being a registered constructor of this TU.
extern "C" void mystery_hook (void);
__attribute__ ((section (".init_array"), used))
void (*extra_entry) (void) = mystery_hook;

__attribute__ ((noinline)) void boot_walk ()
{
  for (void (**ctor) (void) = __init_array_start;
       ctor < __init_array_end; ctor++)
    (*ctor) ();
}

// A plain indirect call.
__attribute__ ((noinline)) void dispatch (void (*fn) (void))
{
  fn ();
}

void kernel_body (void (*cb) (void))
{
  boot_walk ();
  dispatch (cb);
  auto x = __builtin_rvtt_sfpreadlreg (0);
  auto s = __builtin_rvtt_sfpreadlreg (1);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto prod = __builtin_rvtt_sfpmul (x, s, 0);
      x = __builtin_rvtt_sfpaddi (nullptr, prod, 0x42fe, 0, 0, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}
