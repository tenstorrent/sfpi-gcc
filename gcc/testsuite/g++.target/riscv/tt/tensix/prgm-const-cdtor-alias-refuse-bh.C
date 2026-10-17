// Check that an unauditable raw opcode in a cdtor-alias-resolved destructor body refuses the TU.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -fno-early-inlining -mtt-tensix-optimize-prgm-const -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump "prgm-const: refused .opaque-region-undeclared.: unaudited raw opcode" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "prgm-const: allocated" "rvtt_prgm_const" } }
// { dg-final { scan-assembler-not "SFPCONFIG" } }
// { dg-final { scan-assembler "SFPADDI" } }

// The first function through the pass computes the TU facts while the
// guard consumer below still holds its pre-inline-transform body: the
// destructor call is live, its D1 symtab node is already removed, and
// only the caller's call edge still leads to the body.
void first_through_the_pass () {}

volatile unsigned sink;

template <int ID>
struct guard
{
  bool open = false;
  guard (const guard &) = delete;
  __attribute__((always_inline)) inline guard () { open = true; }
  ~guard ()
  {
    if (open)
      sink = ID;
    // An unaudited raw SFPU-class opcode inside the resolved body.
    __asm__ __volatile__ (".ttinsn %0" : : "n" (0x81000000));
  }
};

void
guarded_kernel ()
{
  guard<11> g;
  auto x = __builtin_rvtt_sfpreadlreg (0);
  auto s = __builtin_rvtt_sfpreadlreg (1);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto prod = __builtin_rvtt_sfpmul (x, s, 0);
      x = __builtin_rvtt_sfpaddi (nullptr, prod, 0x42fe, 0, 0, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}
