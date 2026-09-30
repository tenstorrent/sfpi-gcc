// Near miss for the cdtor-alias resolution: the resolved destructor
// body must be SCANNED, never presumed clean.  Here the comdat
// destructor body carries an unauditable raw SFPU-class word, so the
// call-edge/alias resolution reaches a real body whose scan refuses --
// the whole TU refuses by name and no allocation fires, on BOTH build
// recipes (alias-of-D2 under comdat groups, full D1 body without).
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -fno-early-inlining -mtt-tensix-optimize-prgm-const -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump "prgm-const: refused .opaque-region-undeclared.: unaudited raw opcode" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "prgm-const: allocated" "rvtt_prgm_const" } }
// { dg-final { scan-assembler-not "SFPCONFIG" } }
// { dg-final { scan-assembler "SFPADDI" } }

// The first function through the pass computes the TU facts eagerly,
// while the guard consumer below still carries its PRE-inline-transform
// body: the destructor call statement is live, its D1 decl's symtab
// node is already swept, and only the caller's call edge still leads to
// the body (the kernel's run_kernel-then-main shape).
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
